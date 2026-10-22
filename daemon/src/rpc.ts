import { existsSync } from "node:fs";
import { homedir } from "node:os";
import type { AgentManager } from "./agents/manager.js";
import type { Config } from "./config.js";
import { logger } from "./log.js";
import { expandHome } from "./paths.js";
import { killProc, listProcs } from "./procs.js";
import { listProjects } from "./projects.js";
import { canUpdateHost, latestFirmware, startHostUpdate } from "./devupdate.js";
import { addHarness, applyField, GROUPS, groupFields, localDownloadBytes, removeHarness, setEnvVar } from "./remotecfg.js";
import { installLocalVoice, localVoiceStatus } from "./voice/local/models.js";
import { compareVersions, OTA_MIN_FW, type OtaStore } from "./ota.js";
import { VERSION } from "./paths.js";
import { KIND_JSON, KIND_MIC, KIND_OTA, KIND_SPK, type DevicePeer, type SecureChannel } from "./session.js";
import type { Stats } from "./stats.js";
import { PtyTerm, TermView, type TermUpdate } from "./terminal.js";
import { transcribe } from "./voice/stt.js";
import { DEVICE_RATE, speakable, synthesizeStream } from "./voice/tts.js";

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
  ota: OtaStore;
  /** Saves a changed config and makes it the live one. */
  saveConfig(cfg: Config): void;
  /** Restarts the login service soon after the reply goes out. Returns a message for the device. */
  restart(): string;
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

/** Largest JSON message the device parses safely next to its other buffers (it has no PSRAM). */
export const DEVICE_JSON_MAX = 32 * 1024;

/** The newest lines that fit in `maxBytes`, each cut to `maxChars`. */
export function fitLines(lines: string[], maxBytes: number, maxChars: number): string[] {
  const out: string[] = [];
  let bytes = 0;
  for (let i = lines.length - 1; i >= 0; i--) {
    const l = lines[i].length > maxChars ? `${lines[i].slice(0, maxChars - 3)}...` : lines[i];
    bytes += Buffer.byteLength(l) + 4;
    if (bytes > maxBytes) break;
    out.unshift(l);
  }
  return out;
}

/** One authenticated device connection. */
interface Player { cancelled: boolean; push(pcm: Buffer): void; end(): void }

export class DeviceSession {
  readonly topics = new Set<string>();
  readonly since = Date.now();
  /** Stats interval the device asked for in `sub`, and when it last got stats. */
  statsMs = 2000;
  statsAt = 0;
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

  /** True when this session subscribed to stats and its interval has passed. */
  statsDue(now: number): boolean {
    return this.topics.has("stats") && now - this.statsAt >= this.statsMs - 250;
  }

  event(e: string, d: unknown): boolean {
    return this.send({ t: "ev", e, d }, `event ${e}`);
  }

  /** Sends one JSON message, or drops it when the device could not hold it. */
  /** Starts the local model download and tells the device when it ends. Returns the reply text. */
  private installVoice(): string {
    const cfg = this.ctx.cfg(), st = localVoiceStatus(cfg);
    if (!st.addon) throw new Error(`local voice does not run on ${st.platform}: ${st.addonError ?? "sherpa-onnx-node is missing"}`);
    if (st.installing) return "The install is already running";
    const bytes = localDownloadBytes(cfg);
    if (!bytes) return "The local models are already installed";
    installLocalVoice(cfg).then(
      () => this.event("notify", { kind: "done", title: "Local voice ready", body: `${this.ctx.host().name} can now do speech without API keys` }),
      (e: unknown) => this.event("notify", { kind: "error", title: "Local voice install failed", body: e instanceof Error ? e.message : String(e) }),
    );
    log.info(`${this.device.name} started the local voice install`);
    return `Downloading ${Math.round(bytes / 1e6)} MB on the host`;
  }

  /** A device that may change harness commands can run anything, so this follows allowShell. */
  private configWritable(): void {
    if (!this.ctx.cfg().allowShell) throw new Error("host settings are read-only because allowShell is false. Change them on the host");
  }

  private send(msg: object, what: string): boolean {
    const body = Buffer.from(JSON.stringify(msg), "utf8");
    if (body.length > DEVICE_JSON_MAX) {
      log.warn(`dropped ${what} for "${this.device.name}": ${body.length} bytes is over the device limit`);
      return false;
    }
    return this.ch.sendRaw(KIND_JSON, body);
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
      if (!this.send({ t: "res", id: m.id, ok: true, r: r ?? {} }, `reply to ${m.m}`)) throw new Error("reply too large for the device");
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
        this.statsMs = clamp(num(p, "ms", this.ctx.cfg().statsMs), 500, 3_600_000);
        const stats = this.ctx.latestStats();
        if (this.topics.has("stats") && stats) {
          this.statsAt = Date.now();
          setTimeout(() => this.event("stats", stats), 10);
        }
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
      "agent.tail": async (p) => ({ lines: fitLines(await a.tail(str(p, "id"), clamp(num(p, "n", 40), 1, 200)), 12_000, 400) }),
      "agent.last": async (p) => ({ text: (await a.last(str(p, "id"))).slice(0, 4000) }),
      "agent.speak": async (p) => {
        const text = await a.last(str(p, "id"));
        if (!speakable(text, 10)) throw new Error("no reply to read yet");
        return { ms: await this.say(text, str(p, "voice", false)) };
      },
      // The data goes out as one KIND_OTA frame (u32be offset, bytes) just before this reply.
      "config.groups": () => ({ groups: GROUPS }),
      "config.fields": (p) => ({ fields: groupFields(this.ctx.cfg(), str(p, "group")) }),
      "config.set": (p) => {
        this.configWritable();
        const key = str(p, "key");
        if (key.startsWith("env.")) {
          setEnvVar(key.slice(4), p.value === null ? undefined : String(p.value ?? ""));
          log.info(`${this.device.name} ${p.value === null ? "removed" : "set"} ${key.slice(4)}`);
          return { restart: false };
        }
        const { cfg, restart } = applyField(structuredClone(this.ctx.cfg()), key, p.value);
        this.ctx.saveConfig(cfg);
        log.info(`${this.device.name} set ${key}`);
        return { restart };
      },
      "config.add": (p) => {
        this.configWritable();
        const kind = str(p, "kind"), id = str(p, "id"), value = str(p, "value", false);
        if (kind === "env") setEnvVar(id, value);
        else if (kind === "harness") this.ctx.saveConfig(addHarness(structuredClone(this.ctx.cfg()), id, value));
        else throw new Error(`cannot add "${kind}"`);
        log.info(`${this.device.name} added ${kind} ${id}`);
        return {};
      },
      "config.action": (p) => {
        this.configWritable();
        const key = str(p, "key");
        if (key === "restart") return { message: this.ctx.restart() };
        if (key === "voice.install") return { message: this.installVoice() };
        if (!key.startsWith("remove:")) throw new Error(`no action "${key}"`);
        this.ctx.saveConfig(removeHarness(structuredClone(this.ctx.cfg()), key.slice(7)));
        return { message: "Removed" };
      },
      "update.check": async (p) => {
        const { fw, note } = await latestFirmware(p.force === true);
        const cur = this.device.fw;
        return {
          fw: cur, latest: fw?.version ?? "", source: fw?.source ?? "", size: fw?.app.length ?? 0,
          newer: !!fw && compareVersions(fw.version, cur) > 0, linkOk: compareVersions(cur, OTA_MIN_FW) >= 0,
          host: VERSION, hostUpdate: canUpdateHost(), note: note.slice(0, 300),
        };
      },
      // The device asked for this, so the offer carries `asked` and the device skips its own prompt.
      "update.firmware": async () => {
        const { fw } = await latestFirmware();
        if (!fw) throw new Error("no firmware found on the host (needs gh for releases, or a build in firmware/dist)");
        this.ctx.ota.offer(this.device.id, this.device.name, fw);
        this.event("ota.offer", { ver: fw.version, size: fw.app.length, sha256: fw.sha256, from: this.ctx.host().name, asked: true });
        log.info(`${this.device.name} asked for firmware ${fw.version || "(no version)"}`);
        return { version: fw.version, size: fw.app.length };
      },
      "update.host": () => {
        const logPath = startHostUpdate();
        log.info(`${this.device.name} started a host update (log: ${logPath})`);
        return { started: true, log: logPath };
      },
      "ota.read": (p) => {
        const off = num(p, "off", -1);
        const chunk = this.ctx.ota.read(this.device.id, off, clamp(num(p, "n", 8192), 1, 65536));
        const head = Buffer.alloc(4);
        head.writeUInt32BE(off);
        if (this.ch.closed) throw new Error("link closed");
        this.ch.sendRaw(KIND_OTA, Buffer.concat([head, chunk]));
        return { n: chunk.length };
      },
      "ota.done": (p) => {
        const ok = p.ok === true;
        this.ctx.ota.finish(this.device.id, ok, str(p, "err", false), str(p, "dest", false));
        log.info(`"${this.device.name}" firmware update ${ok ? `done (${str(p, "dest", false) || "flash"})` : `failed: ${str(p, "err", false)}`}`);
        return {};
      },
      "agent.permit": async (p) => {
        await a.permit(str(p, "id"), p.allow === true, str(p, "message", false).slice(0, 2000) || undefined);
        return {};
      },
      "agent.pending": async (p) => {
        const ask = await a.pending(str(p, "id"));
        return { tool: (ask?.tool ?? "").slice(0, 80), text: (ask?.text ?? "").slice(0, 3000) };
      },
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
      const cwd = expandHome(str(p, "cwd", false) || "~");
      src = new PtyTerm(cfg.shell, ["-l"], { cwd: existsSync(cwd) ? cwd : homedir(), cols, rows });
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

  /** Streams speech to the device as it is made. Resolves once audio starts, so a long text cannot time out the call. */
  async say(text: string, voice?: string): Promise<number> {
    const cfg = this.ctx.cfg();
    const clean = speakable(text, cfg.voice.tts.maxChars);
    if (!clean) return 0;
    let out: Player | undefined, bytes = 0;
    const ms = (): number => Math.round((bytes / 2 / DEVICE_RATE) * 1000);
    return new Promise<number>((resolve, reject) => {
      synthesizeStream(cfg, clean, voice, (pcm) => {
        if (!out) { out = this.player(); resolve(ms() + Math.round((pcm.length / 2 / DEVICE_RATE) * 1000)); }
        if (out.cancelled) return false;
        out.push(pcm);
        bytes += pcm.length;
        return true;
      }).then(
        () => { out?.end(); resolve(ms()); },
        (e: unknown) => { out?.end(); if (out) log.warn("speech stopped early", e); else reject(e); },
      );
    });
  }

  /** Paces PCM to the device about 400 ms ahead of playback. Audio can keep arriving until end(). */
  private player(): Player {
    this.stopTts();
    const CHUNK = DEVICE_RATE / 10 * 2;
    let pcm = Buffer.alloc(0), off = 0, playEnd = Date.now(), ended = false;
    let timer: NodeJS.Timeout | undefined;
    const p: Player = {
      cancelled: false,
      push: (b) => { pcm = Buffer.concat([pcm.subarray(off), b]); off = 0; if (!timer) pump(); },
      end: () => { ended = true; if (!timer) pump(); },
    };
    this.event("tts", { state: "start", rate: DEVICE_RATE });
    const pump = (): void => {
      timer = undefined;
      if (p.cancelled) return;
      // After a gap between sentences, count from now so the device buffer does not overflow.
      while (pcm.length - off >= (ended ? 1 : CHUNK) && playEnd - Date.now() < 400) {
        const piece = pcm.subarray(off, off + CHUNK);
        this.ch.sendRaw(KIND_SPK, piece);
        off += piece.length;
        playEnd = Math.max(playEnd, Date.now()) + (piece.length / 2 / DEVICE_RATE) * 1000;
      }
      if (ended && off >= pcm.length) {
        timer = setTimeout(() => { if (!p.cancelled) { this.tts = undefined; this.event("tts", { state: "end" }); } }, Math.max(0, playEnd - Date.now()));
        return;
      }
      if (pcm.length - off >= (ended ? 1 : CHUNK)) timer = setTimeout(pump, 40);
    };
    this.tts = {
      cancel: () => {
        p.cancelled = true;
        if (timer) clearTimeout(timer);
        this.event("tts", { state: "end" });
      },
    };
    return p;
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
