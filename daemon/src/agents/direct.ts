import { EventEmitter } from "node:events";
import { randomUUID } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { homedir } from "node:os";
import { basename, join } from "node:path";
import type { Config } from "../config.js";
import { harnessInfo, resolveHarnesses, type ResolvedHarness } from "../harness.js";
import { expandHome } from "../paths.js";
import { PtyTerm } from "../terminal.js";
import type { AgentBackend, AgentInfo, AgentStatus, CreateParams, HarnessInfo, TermTarget } from "./types.js";

const IDLE_MS = 2500;

const WAITING = [
  /Do you want to (proceed|make this edit|create|run|allow|overwrite)/i,
  /\b(Allow|Approve|Permit)\b[^\n]{0,60}\?/,
  /\[y\/n\]|\(y\/n\)|\[Y\/n\]/,
  /Yes, (and )?(don't|do not) ask again/i,
  /waiting for (your )?approval/i,
  /requires (your )?approval/i,
];

const CHROME = /for shortcuts|esc to interrupt|ctrl\+c to|shift\+tab|\? for help|auto-accept|bypass permissions/i;

interface DirectAgent {
  id: string;
  name: string;
  harness: ResolvedHarness;
  cwd: string;
  term: PtyTerm;
  started: number;
  status: AgentStatus;
  sessionId?: string;
  pendingPrompt?: string;
}

export class DirectBackend extends EventEmitter implements AgentBackend {
  readonly kind = "direct" as const;
  private agents = new Map<string, DirectAgent>();
  private seq = 1;
  private timer: NodeJS.Timeout;

  constructor(private readonly cfg: () => Config) {
    super();
    this.timer = setInterval(() => this.tick(), 500);
    this.timer.unref();
  }

  available(): boolean {
    return true;
  }

  owns(id: string): boolean {
    return this.agents.has(id);
  }

  async harnesses(): Promise<HarnessInfo[]> {
    return resolveHarnesses(this.cfg().harness).map(harnessInfo);
  }

  async list(): Promise<AgentInfo[]> {
    const now = Date.now();
    return [...this.agents.values()].map((a) => ({
      id: a.id, name: a.name, harness: `direct:${a.harness.id}`, backend: "direct",
      status: a.status, cwd: a.cwd, age: Math.round((now - a.started) / 1000),
      attn: a.status === "waiting", last: lastLine(a.term.screen()),
    }));
  }

  async create(p: CreateParams): Promise<string> {
    const hid = p.harness.replace(/^direct:/, "");
    const h = resolveHarnesses(this.cfg().harness).find((x) => x.id === hid);
    if (!h) throw new Error(`unknown harness ${p.harness}`);
    if (!h.path) throw new Error(`${h.name} is not installed (${h.bins.join(" or ")})`);
    const cwd = expandHome(p.cwd || "~");
    if (!existsSync(cwd)) throw new Error(`no such directory: ${p.cwd}`);
    const mode = h.modes.find((m) => m.id === p.mode) ?? h.modes[0];
    const sessionId = h.sessionArgs ? randomUUID() : undefined;
    const promptArgs = p.prompt ? h.promptArgs(p.prompt) : [];
    const args = [...h.baseArgs, ...mode.args, ...(sessionId ? h.sessionArgs!(sessionId) : []), ...(promptArgs ?? [])];
    const term = new PtyTerm(h.path, args, { cwd, cols: 100, rows: 30 });
    const id = `d-${this.seq++}`;
    const name = p.name || summarize(p.prompt) || `${h.name} in ${basename(cwd)}`;
    const agent: DirectAgent = { id, name, harness: h, cwd, term, started: Date.now(), status: "working", sessionId };
    if (promptArgs === null && p.prompt) agent.pendingPrompt = p.prompt;
    term.on("exit", () => { agent.status = "exited"; this.emit("changed"); });
    this.agents.set(id, agent);
    this.emit("changed");
    return id;
  }

  private get(id: string): DirectAgent {
    const a = this.agents.get(id);
    if (!a) throw new Error(`no such agent ${id}`);
    return a;
  }

  async send(id: string, text: string): Promise<void> {
    const a = this.get(id);
    a.term.write(text);
    // TUIs treat "text\r" in one chunk as a paste with a newline, so submit separately.
    await new Promise((r) => setTimeout(r, 150));
    a.term.write("\r");
  }

  async stop(id: string): Promise<void> {
    const a = this.get(id);
    a.term.write(a.harness.interrupt);
  }

  async kill(id: string): Promise<void> {
    const a = this.get(id);
    a.term.kill();
    this.agents.delete(id);
    this.emit("changed");
  }

  async tail(id: string, n: number): Promise<string[]> {
    return this.get(id).term.tail(n);
  }

  async last(id: string): Promise<string> {
    const a = this.get(id);
    if (a.harness.id === "claude" && a.sessionId) {
      const text = claudeLastMessage(a.cwd, a.sessionId);
      if (text) return text;
    }
    return a.term.screen().map(stripChrome).filter((s) => s && !CHROME.test(s)).slice(-12).join("\n");
  }

  async permit(id: string, allow: boolean): Promise<void> {
    const a = this.get(id);
    a.term.write(allow ? a.harness.permitYes : a.harness.permitNo);
  }

  termTarget(id: string): TermTarget {
    this.get(id);
    return { kind: "attach", agentId: id };
  }

  term(id: string): PtyTerm {
    return this.get(id).term;
  }

  private tick(): void {
    let changed = false;
    const now = Date.now();
    for (const a of this.agents.values()) {
      if (a.term.exited) continue;
      let next: AgentStatus = now - a.term.lastOutput < IDLE_MS ? "working" : "idle";
      if (next === "idle") {
        const text = a.term.screen().slice(-16).join("\n");
        if (WAITING.some((re) => re.test(text))) next = "waiting";
        if (a.pendingPrompt && now - a.started > 1500) {
          const prompt = a.pendingPrompt;
          a.pendingPrompt = undefined;
          void this.send(a.id, prompt);
        }
      }
      if (next !== a.status) {
        a.status = next;
        changed = true;
      }
    }
    if (changed) this.emit("changed");
  }

  shutdown(): void {
    clearInterval(this.timer);
    for (const a of this.agents.values()) a.term.kill();
  }
}

function stripChrome(s: string): string {
  return s.replace(/^[\s│┃|╭╮╰╯─━>❯]+|[\s│┃|╭╮╰╯─━]+$/g, "").trim();
}

function lastLine(screen: string[]): string {
  for (let i = screen.length - 1; i >= 0; i--) {
    const s = stripChrome(screen[i]);
    if ((s.match(/[A-Za-z0-9]/g)?.length ?? 0) >= 3 && !CHROME.test(s)) return s.slice(0, 120);
  }
  return "";
}

function summarize(prompt: string): string {
  const s = prompt.replace(/\s+/g, " ").trim();
  return s.length > 40 ? s.slice(0, 39) + "…" : s;
}

function claudeLastMessage(cwd: string, sessionId: string): string {
  const file = join(homedir(), ".claude", "projects", cwd.replace(/[^A-Za-z0-9]/g, "-"), `${sessionId}.jsonl`);
  if (!existsSync(file)) return "";
  const lines = readFileSync(file, "utf8").trimEnd().split("\n");
  for (let i = lines.length - 1; i >= 0; i--) {
    try {
      const e = JSON.parse(lines[i]) as { type?: string; message?: { content?: { type: string; text?: string }[] | string } };
      if (e.type !== "assistant") continue;
      const c = e.message?.content;
      const text = typeof c === "string" ? c : (c ?? []).filter((p) => p.type === "text").map((p) => p.text ?? "").join("\n");
      if (text.trim()) return text.trim();
    } catch { /* partial line */ }
  }
  return "";
}
