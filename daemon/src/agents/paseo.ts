import { EventEmitter } from "node:events";
import { execFile } from "node:child_process";
import type { Config } from "../config.js";
import { which } from "../env.js";
import { harnessColor } from "../harness.js";
import { expandHome } from "../paths.js";
import type { AgentBackend, AgentInfo, AgentStatus, CreateParams, HarnessInfo, ModeInfo, TermTarget } from "./types.js";

/** Paseo lists mode labels only; these are the IDs `paseo run --mode` takes. */
const MODE_IDS: Record<string, Record<string, string>> = {
  claude: { "Plan Mode": "plan", "Always Ask": "default", "Accept File Edits": "acceptEdits", "Auto mode": "auto", Bypass: "bypassPermissions" },
  codex: { "Default Permissions": "auto", "Auto-review": "auto-review", "Full Access": "full-access" },
};

interface PaseoAgentRow {
  id: string;
  name?: string;
  provider?: string;
  status?: string;
  cwd?: string;
  created?: string;
}

interface PaseoProviderRow {
  provider: string;
  label: string;
  status: string;
  enabled: string;
  defaultMode?: string;
  modes?: string;
}

interface PermitRow {
  agentId: string;
}

const ANSI = /\x1b\[[0-9;?]*[ -/]*[@-~]/g;

export class PaseoBackend extends EventEmitter implements AgentBackend {
  readonly kind = "paseo" as const;
  private ok = false;
  private checkedAt = 0;
  private known = new Set<string>();
  private cache?: { at: number; list: AgentInfo[] };

  constructor(private readonly cfg: () => Config) {
    super();
  }

  private get bin(): string {
    return this.cfg().paseo.bin;
  }

  private run(args: string[], timeoutMs = 20_000): Promise<string> {
    return new Promise((resolve, reject) => {
      execFile(this.bin, args, { timeout: timeoutMs, maxBuffer: 8 << 20 }, (err, stdout, stderr) => {
        if (err) reject(new Error((stderr || err.message).toString().trim().split("\n").pop() ?? "paseo failed"));
        else resolve(stdout.toString());
      });
    });
  }

  available(): boolean {
    const want = this.cfg().paseo.enabled;
    if (want === false) return false;
    if (Date.now() - this.checkedAt > 30_000) {
      this.checkedAt = Date.now();
      if (!which(this.bin)) this.ok = false;
      else this.run(["ls", "--json"], 8000).then(() => { this.ok = true; }, () => { this.ok = false; });
    }
    return this.ok;
  }

  owns(id: string): boolean {
    return this.known.has(id) || /^[0-9a-f]{7,}(-[0-9a-f-]+)?$/i.test(id);
  }

  async harnesses(): Promise<HarnessInfo[]> {
    if (!this.available()) return [];
    const rows = JSON.parse(await this.run(["provider", "ls", "--json"])) as PaseoProviderRow[];
    return rows.filter((r) => r.status === "available" && r.enabled === "Enabled").map((r) => {
      const table = MODE_IDS[r.provider] ?? {};
      const modes: ModeInfo[] = [{ id: "", label: "Default" }];
      for (const label of (r.modes ?? "").split(",").map((s) => s.trim()).filter(Boolean)) {
        if (table[label]) modes.push({ id: table[label], label });
      }
      return { id: `paseo:${r.provider}`, name: r.label, backend: "paseo", ok: true, color: harnessColor(r.provider), modes, mode: "" };
    });
  }

  async list(): Promise<AgentInfo[]> {
    if (!this.available()) return [];
    if (this.cache && Date.now() - this.cache.at < 1500) return this.cache.list;
    const [rows, permits] = await Promise.all([
      this.run(["ls", "--json"]).then((s) => JSON.parse(s) as PaseoAgentRow[]),
      this.run(["permit", "ls", "--json"]).then((s) => JSON.parse(s) as PermitRow[]).catch(() => [] as PermitRow[]),
    ]);
    const waiting = new Set(permits.map((p) => p.agentId));
    const list: AgentInfo[] = rows.slice(0, 40).map((r) => {
      const provider = (r.provider ?? "").split("/")[0];
      let status = mapStatus(r.status);
      if (waiting.has(r.id)) status = "waiting";
      return {
        id: r.id, name: r.name ?? r.id.slice(0, 7), harness: `paseo:${provider}`, backend: "paseo",
        status, cwd: r.cwd ?? "", age: parseAge(r.created), attn: status === "waiting", last: "",
      };
    });
    this.known = new Set(list.map((a) => a.id));
    this.cache = { at: Date.now(), list };
    return list;
  }

  async create(p: CreateParams): Promise<string> {
    const provider = p.harness.replace(/^paseo:/, "");
    const args = ["run", "--no-wait", "-q", "--provider", provider, "--cwd", expandHome(p.cwd || "~")];
    if (p.name) args.push("--title", p.name);
    if (p.mode) args.push("--mode", p.mode);
    args.push("--", p.prompt);
    const out = (await this.run(args, 60_000)).trim();
    const id = /[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}/i.exec(out)?.[0] ?? out.split(/\s+/).pop() ?? "";
    if (!id) throw new Error("paseo run returned no agent id");
    this.known.add(id);
    this.cache = undefined;
    this.emit("changed");
    return id;
  }

  async send(id: string, text: string): Promise<void> {
    await this.run(["send", "--no-wait", id, "--prompt", text]);
    this.cache = undefined;
    this.emit("changed");
  }

  async stop(id: string): Promise<void> {
    await this.run(["stop", id]);
    this.cache = undefined;
  }

  async kill(id: string): Promise<void> {
    await this.run(["archive", id]);
    this.cache = undefined;
    this.emit("changed");
  }

  async tail(id: string, n: number): Promise<string[]> {
    const out = await this.run(["logs", id, "--tail", String(n), "--no-color"]);
    return out.replace(ANSI, "").trimEnd().split("\n").slice(-n * 4);
  }

  async last(id: string): Promise<string> {
    // Typed entries start with "[Thought]", "[Shell]" and so on; assistant text has no prefix.
    const out = await this.run(["logs", id, "--filter", "text", "--tail", "12", "--no-color"]);
    const lines = out.replace(ANSI, "").trimEnd().split("\n");
    const block: string[] = [];
    for (let i = lines.length - 1; i >= 0 && !/^\[[A-Za-z ]+\]/.test(lines[i]); i--) block.unshift(lines[i]);
    return block.join("\n").trim();
  }

  async permit(id: string, allow: boolean): Promise<void> {
    await this.run(["permit", allow ? "allow" : "deny", id]);
    this.cache = undefined;
    this.emit("changed");
  }

  termTarget(id: string): TermTarget {
    return { kind: "spawn", cmd: this.bin, args: ["attach", id], cwd: expandHome("~") };
  }
}

function mapStatus(s?: string): AgentStatus {
  switch ((s ?? "").toLowerCase()) {
    case "running": case "working": case "starting": return "working";
    case "idle": case "ready": return "idle";
    case "error": case "failed": return "error";
    case "closed": case "stopped": case "exited": return "exited";
    default: return "idle";
  }
}

const UNIT: Record<string, number> = { second: 1, minute: 60, hour: 3600, day: 86400, week: 604800, month: 2592000, year: 31536000 };

export function parseAge(rel?: string): number {
  if (!rel) return 0;
  const m = /(\d+|an?|one)\s+(second|minute|hour|day|week|month|year)s?\s+ago/i.exec(rel);
  if (!m) return 0;
  const n = /^\d+$/.test(m[1]) ? parseInt(m[1], 10) : 1;
  return n * UNIT[m[2].toLowerCase()];
}
