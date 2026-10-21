import { EventEmitter } from "node:events";
import type { Config } from "../config.js";
import { logger } from "../log.js";
import { DirectBackend } from "./direct.js";
import { PaseoBackend } from "./paseo.js";
import type { AgentBackend, AgentInfo, AgentStatus, CreateParams, HarnessInfo, NotifyEvent } from "./types.js";

const log = logger("agents");
const RANK: Record<AgentStatus, number> = { waiting: 0, working: 1, idle: 2, error: 3, exited: 4 };

/** Merges agent backends and emits "agents" (list changed) and "notify" (status transitions). */
export class AgentManager extends EventEmitter {
  readonly direct: DirectBackend;
  readonly paseo: PaseoBackend;
  private backends: AgentBackend[];
  private snapshot: AgentInfo[] = [];
  private snapshotKey = "";
  private seeded = false;
  private workingSince = new Map<string, number>();
  private timer?: NodeJS.Timeout;
  private refreshing?: Promise<void>;

  constructor(cfg: () => Config) {
    super();
    this.direct = new DirectBackend(cfg);
    this.paseo = new PaseoBackend(cfg);
    this.backends = [this.direct, this.paseo];
    for (const b of this.backends) b.on("changed", () => void this.refresh());
  }

  startPolling(): void {
    this.paseo.available();
    this.timer = setInterval(() => void this.refresh(), 3000);
    void this.refresh();
  }

  shutdown(): void {
    if (this.timer) clearInterval(this.timer);
    this.direct.shutdown();
  }

  list(): AgentInfo[] {
    return this.snapshot;
  }

  counts(): { working: number; idle: number; waiting: number } {
    const c = { working: 0, idle: 0, waiting: 0 };
    for (const a of this.snapshot) {
      if (a.status === "working") c.working++;
      else if (a.status === "waiting") c.waiting++;
      else if (a.status === "idle") c.idle++;
    }
    return c;
  }

  refresh(): Promise<void> {
    this.refreshing ??= this.doRefresh().finally(() => { this.refreshing = undefined; });
    return this.refreshing;
  }

  private async doRefresh(): Promise<void> {
    const lists = await Promise.all(this.backends.map((b) => b.list().catch((e: Error) => {
      log.debug(`${b.kind} list failed`, e);
      return this.snapshot.filter((a) => a.backend === b.kind);
    })));
    const merged = lists.flat().sort((a, b) => RANK[a.status] - RANK[b.status] || a.age - b.age);
    this.detectTransitions(merged);
    const key = JSON.stringify(merged.map((a) => [a.id, a.status, a.name, a.last, a.attn]));
    this.snapshot = merged;
    if (key !== this.snapshotKey) {
      this.snapshotKey = key;
      this.emit("agents", merged);
    }
  }

  private detectTransitions(next: AgentInfo[]): void {
    const prev = new Map(this.snapshot.map((a) => [a.id, a.status]));
    if (!this.seeded) {
      this.seeded = true;
      return;
    }
    const now = Date.now();
    for (const a of next) {
      const was = prev.get(a.id);
      // A turn runs from the first "working" until idle or exited; waiting for approval stays inside it.
      if (a.status === "working" && !this.workingSince.has(a.id)) this.workingSince.set(a.id, now);
      if (!was || was === a.status) continue;
      let ev: NotifyEvent | undefined;
      if (a.status === "waiting") ev = { kind: "attn", title: `${a.name}`, body: "Needs approval", agent: a.id };
      else if (a.status === "error") ev = { kind: "error", title: `${a.name}`, body: "Agent error", agent: a.id };
      else if (was === "working" && (a.status === "idle" || a.status === "exited")) {
        const since = this.workingSince.get(a.id);
        ev = { kind: "done", title: `${a.name}`, body: a.last || "Finished", agent: a.id };
        if (since !== undefined) ev.secs = Math.round((now - since) / 1000);
      }
      if (a.status === "idle" || a.status === "exited" || a.status === "error") this.workingSince.delete(a.id);
      if (ev) this.emit("notify", ev);
    }
    for (const id of this.workingSince.keys()) if (!next.some((a) => a.id === id)) this.workingSince.delete(id);
  }

  private route(id: string): AgentBackend {
    const b = this.backends.find((x) => x.owns(id));
    if (!b) throw new Error(`no such agent ${id}`);
    return b;
  }

  async harnesses(): Promise<HarnessInfo[]> {
    const lists = await Promise.all(this.backends.map((b) => b.harnesses().catch(() => [] as HarnessInfo[])));
    return lists.flat().sort((a, b) => Number(b.ok) - Number(a.ok) || (a.backend === "paseo" ? -1 : 1) - (b.backend === "paseo" ? -1 : 1));
  }

  async create(p: CreateParams): Promise<string> {
    if (!p.harness) throw new Error("harness required");
    const backend = p.harness.startsWith("paseo:") ? this.paseo : this.direct;
    const id = await backend.create(p);
    log.info(`created ${id} (${p.harness}) in ${p.cwd}`);
    void this.refresh();
    return id;
  }

  send(id: string, text: string): Promise<void> { return this.route(id).send(id, text); }
  stop(id: string): Promise<void> { return this.route(id).stop(id); }
  kill(id: string): Promise<void> { return this.route(id).kill(id); }
  tail(id: string, n: number): Promise<string[]> { return this.route(id).tail(id, n); }
  last(id: string): Promise<string> { return this.route(id).last(id); }
  permit(id: string, allow: boolean): Promise<void> { return this.route(id).permit(id, allow); }
  termTarget(id: string): ReturnType<AgentBackend["termTarget"]> { return this.route(id).termTarget(id); }
}
