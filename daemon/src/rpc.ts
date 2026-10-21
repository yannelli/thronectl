import type { AgentManager } from "./agents/manager.js";
import type { Config } from "./config.js";
import { logger } from "./log.js";
import { expandHome } from "./paths.js";
import { killProc, listProcs } from "./procs.js";
import { listProjects } from "./projects.js";
import { KIND_MIC, KIND_SPK, type DevicePeer, type SecureChannel } from "./session.js";
import type { Stats } from "./stats.js";
import { PtyTerm, TermView, type TermUpdate } from "./terminal.js";
import { transcribe } from "./voice/stt.js";
import { DEVICE_RATE, speakable, synthesize } from "./voice/tts.js";

const log = logger("rpc");
const MAX_MIC_BYTES = 16000 * 2 * 90;
const MAX_TERMS = 4;

export interface HostInfo {
  id: string; name: string; hostname: string; os: string; arch: string; user: string; ver: string; paseo: boolean;
  tzOff: number; // minutes east of UTC
  time: number; // unix ms
}

export interface DaemonContext {
  cfg(): Config;
  host(): HostInfo;
  agents: AgentManager;
  latestStats(): Stats | undefined;
  recentCwds(): string[];
  addRecentCwd(cwd: string): void;
}

type Params = Record<string, unknown>;
type Handler = (p: Params) => Promise<unknown> | unknown;

const str = (p: Params, k: string, required = true): string => {
  const v = p[k];
  if (typeof v === "string") return v;
  if (required) throw new Error(`missing ${k}`);
  return "";
};
const num = (p: Params, k: string, def: number): number => (typeof p[k] === "number" ? (p[k] as number) : def);
const clamp = (v: number, lo: number, hi: number): number => Math.max(lo, Math.min(hi, Math.round(v)));

/** One authenticated device connection. */
export class DeviceSession {
  readonly topics = new Set<string>();
  readonly since = Date.now();
  private terms = new Map<number, TermView>();
  private nextTid = 1;
  private mic?: { rate: number; chunks: Buffer[]; bytes: number };
  private tts?: { cancel: () => void };
  private handlers: Record<string, Handler>;

  constructor(readonly ch: SecureChannel, readonly device: DevicePeer, private readonly ctx: DaemonContext) {
    this.handlers = this.buildHandlers();
    ch.on("json", (m: unknown) => void this.onMessage(m as Params));
    ch.on("audio", (buf: Buffer, kind: number) => this.onAudio(buf, kind));
    ch.on("close", () => this.cleanup());
  }

  event(e: string, d: unknown): boolean {
    return this.ch.sendJson({ t: "ev", e, d });
  }

  close(): void {
    this.ch.close();
  }

  private async onMessage(m: Params): Promise<void> {
    if (m.t !== "req" || typeof m.id !== "number" || typeof m.m !== "string") return;
    const handler = this.handlers[m.m];
    try {
      if (!handler) throw new Error(`unknown method ${m.m}`);
      const r = await handler((m.p as Params) ?? {});
      this.ch.sendJson({ t: "res", id: m.id, ok: true, r: r ?? {} });
    } catch (e) {
      const err = e instanceof Error ? e.message : String(e);
      log.debug(`${m.m} failed: ${err}`);
      this.ch.sendJson({ t: "res", id: m.id, ok: false, err: err.slice(0, 200) });
    }
  }

  private onAudio(buf: Buffer, kind: number): void {
    if (kind !== KIND_MIC || !this.mic) return;
    if (this.mic.bytes + buf.length > MAX_MIC_BYTES) return;
    this.mic.chunks.push(buf);
    this.mic.bytes += buf.length;
  }

  private buildHandlers(): Record<string, Handler> {
    const a = this.ctx.agents;
    return {
      ping: () => ({ t: Date.now() }),
      sub: (p) => {
        this.topics.clear();
        for (const t of (p.topics as string[] | undefined) ?? []) this.topics.add(t);
        const stats = this.ctx.latestStats();
        if (this.topics.has("stats") && stats) setTimeout(() => this.event("stats", stats), 10);
        if (this.topics.has("agents")) setTimeout(() => this.event("agents", { agents: a.list() }), 10);
        return {};
      },
      "stats.get": () => this.ctx.latestStats() ?? {},
      "procs.list": async (p) => ({ procs: await listProcs(p.sort === "mem" ? "mem" : "cpu", num(p, "n", 20)) }),
      "procs.kill": (p) => { killProc(num(p, "pid", 0), str(p, "sig", false) || "TERM"); return {}; },
      "harness.list": async () => ({ harness: await a.harnesses() }),
      "projects.list": () => ({ projects: listProjects(this.ctx.cfg(), this.ctx.recentCwds()) }),
      "agents.list": async () => { await a.refresh(); return { agents: a.list() }; },
      "agent.create": async (p) => {
        const cwd = str(p, "cwd", false) || "~";
        const id = await a.create({ harness: str(p, "harness"), cwd, prompt: str(p, "prompt", false), name: str(p, "name", false), mode: str(p, "mode", false) });
        this.ctx.addRecentCwd(cwd);
        return { id };
      },
      "agent.send": async (p) => { await a.send(str(p, "id"), str(p, "text")); return {}; },
      "agent.stop": async (p) => { await a.stop(str(p, "id")); return {}; },
      "agent.kill": async (p) => { await a.kill(str(p, "id")); return {}; },
      "agent.tail": async (p) => ({ lines: await a.tail(str(p, "id"), clamp(num(p, "n", 40), 1, 200)) }),
      "agent.last": async (p) => ({ text: (await a.last(str(p, "id"))).slice(0, 4000) }),
      "agent.speak": async (p) => {
        const text = await a.last(str(p, "id"));
        if (!speakable(text, 10)) throw new Error("no reply to read yet");
        return { ms: await this.say(text, str(p, "voice", false)) };
      },
      "agent.permit": async (p) => { await a.permit(str(p, "id"), p.allow === true); return {}; },
      "term.open": (p) => this.openTerm(p),
      "term.input": (p) => {
        const view = this.terms.get(num(p, "tid", -1));
        if (!view) throw new Error("no such terminal");
        view.src.write(str(p, "d").slice(0, 4096));
        return {};
      },
      "term.close": (p) => {
        const tid = num(p, "tid", -1);
        this.terms.get(tid)?.close();
        this.terms.delete(tid);
        return {};
      },
      "voice.start": (p) => {
        this.stopTts();
        this.mic = { rate: clamp(num(p, "rate", DEVICE_RATE), 8000, 48000), chunks: [], bytes: 0 };
        return {};
      },
      "voice.stop": async () => {
        const mic = this.mic;
        this.mic = undefined;
        if (!mic) throw new Error("not recording");
        const text = await transcribe(this.ctx.cfg(), Buffer.concat(mic.chunks), mic.rate);
        log.info(`transcribed ${(mic.bytes / 2 / mic.rate).toFixed(1)}s: ${text.slice(0, 80)}`);
        return { text };
      },
      "voice.cancel": () => { this.mic = undefined; return {}; },
      "tts.say": async (p) => ({ ms: await this.say(str(p, "text"), str(p, "voice", false)) }),
      "tts.stop": () => { this.stopTts(); return {}; },
    };
  }

  private openTerm(p: Params): { tid: number } {
    if (this.terms.size >= MAX_TERMS) throw new Error("too many terminals open");
    const cols = clamp(num(p, "cols", 40), 20, 160);
    const rows = clamp(num(p, "rows", 15), 5, 60);
    const tid = this.nextTid++;
    let src: PtyTerm;
    let owns = true;
    if (typeof p.agent === "string") {
      const target = this.ctx.agents.termTarget(p.agent);
      if (target.kind === "attach") {
        src = this.ctx.agents.direct.term(target.agentId);
        src.resize(cols, rows);
        owns = false;
      } else {
        src = new PtyTerm(target.cmd, target.args, { cwd: target.cwd, cols, rows });
      }
    } else {
      const cfg = this.ctx.cfg();
      if (!cfg.allowShell) throw new Error("shell access is disabled on this host");
      src = new PtyTerm(cfg.shell, ["-l"], { cwd: expandHome(str(p, "cwd", false) || "~"), cols, rows });
    }
    const send = (u: TermUpdate): boolean => {
      if (this.ch.writableLength > 256 * 1024) return false;
      return this.event("term", { tid, ...u });
    };
    const onExit = (code: number): void => {
      this.event("term_exit", { tid, code });
      this.terms.get(tid)?.close();
      this.terms.delete(tid);
    };
    this.terms.set(tid, new TermView(src, send, onExit, owns));
    return { tid };
  }

  /** Synthesizes text and streams it to the device. Resolves with the audio length in ms. */
  async say(text: string, voice?: string): Promise<number> {
    const cfg = this.ctx.cfg();
    const clean = speakable(text, cfg.voice.tts.maxChars);
    if (!clean) return 0;
    const pcm = await synthesize(cfg, clean, voice);
    this.play(pcm);
    return Math.round((pcm.length / 2 / DEVICE_RATE) * 1000);
  }

  private play(pcm: Buffer): void {
    this.stopTts();
    const CHUNK = DEVICE_RATE / 10 * 2;
    const start = Date.now();
    let off = 0, sentMs = 0, cancelled = false;
    let timer: NodeJS.Timeout | undefined;
    this.event("tts", { state: "start", rate: DEVICE_RATE });
    const pump = (): void => {
      if (cancelled) return;
      while (off < pcm.length && sentMs - (Date.now() - start) < 400) {
        this.ch.sendRaw(KIND_SPK, pcm.subarray(off, off + CHUNK));
        off += CHUNK;
        sentMs += 100;
      }
      if (off >= pcm.length) {
        timer = setTimeout(() => { if (!cancelled) { this.tts = undefined; this.event("tts", { state: "end" }); } }, Math.max(0, sentMs - (Date.now() - start)));
        return;
      }
      timer = setTimeout(pump, 40);
    };
    this.tts = {
      cancel: () => {
        cancelled = true;
        if (timer) clearTimeout(timer);
        this.event("tts", { state: "end" });
      },
    };
    pump();
  }

  private stopTts(): void {
    this.tts?.cancel();
    this.tts = undefined;
  }

  private cleanup(): void {
    this.stopTts();
    for (const v of this.terms.values()) v.close();
    this.terms.clear();
    this.mic = undefined;
  }
}
