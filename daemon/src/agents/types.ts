import type { EventEmitter } from "node:events";

export type AgentStatus = "working" | "idle" | "waiting" | "exited" | "error";
export type Backend = "direct" | "paseo";

export interface ModeInfo {
  id: string;
  label: string;
}

export interface HarnessInfo {
  id: string;
  name: string;
  backend: Backend;
  ok: boolean;
  color: number;
  modes: ModeInfo[];
  mode: string;
}

export interface AgentInfo {
  id: string;
  name: string;
  harness: string;
  backend: Backend;
  status: AgentStatus;
  cwd: string;
  age: number;
  attn: boolean;
  last: string;
}

export interface CreateParams {
  harness: string;
  cwd: string;
  prompt: string;
  name?: string;
  mode?: string;
}

export interface NotifyEvent {
  kind: "done" | "attn" | "error" | "info";
  title: string;
  body: string;
  agent?: string;
  /** For "done": how long the turn ran, so devices can skip short turns. */
  secs?: number;
}

/** How to show an agent in a terminal view. */
export type TermTarget =
  | { kind: "attach"; agentId: string }
  | { kind: "spawn"; cmd: string; args: string[]; cwd: string };

/** Emits "changed" when its agent list may have changed. */
export interface AgentBackend extends EventEmitter {
  readonly kind: Backend;
  available(): boolean;
  harnesses(): Promise<HarnessInfo[]>;
  list(): Promise<AgentInfo[]>;
  owns(id: string): boolean;
  create(p: CreateParams): Promise<string>;
  send(id: string, text: string): Promise<void>;
  stop(id: string): Promise<void>;
  kill(id: string): Promise<void>;
  tail(id: string, n: number): Promise<string[]>;
  last(id: string): Promise<string>;
  permit(id: string, allow: boolean): Promise<void>;
  termTarget(id: string): TermTarget;
}

export function rgb565(hex: string): number {
  const v = parseInt(hex.replace("#", ""), 16);
  const r = (v >> 16) & 0xff, g = (v >> 8) & 0xff, b = v & 0xff;
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}
