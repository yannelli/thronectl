import { EventEmitter } from "node:events";
import { randomUUID } from "node:crypto";

export interface PairRequest {
  id: string;
  deviceId: string;
  deviceName: string;
  addr: string;
  code: string;
  fw: string;
  createdAt: number;
  expiresAt: number;
}

export const PAIR_TIMEOUT_MS = 120_000;

/** Holds pairing requests until an approver (daemon TTY or `thronectl pair`) answers yes or no. */
export class PairingBroker extends EventEmitter {
  private pendingMap = new Map<string, { req: PairRequest; done: (yes: boolean) => void }>();

  request(info: Omit<PairRequest, "id" | "createdAt" | "expiresAt">): { req: PairRequest; result: Promise<boolean>; cancel: () => void } {
    const now = Date.now();
    const req: PairRequest = { ...info, id: randomUUID().slice(0, 8), createdAt: now, expiresAt: now + PAIR_TIMEOUT_MS };
    let settle!: (yes: boolean) => void;
    const result = new Promise<boolean>((resolve) => { settle = resolve; });
    const timer = setTimeout(() => finish(false, "timeout"), PAIR_TIMEOUT_MS);
    const finish = (yes: boolean, reason: string): void => {
      if (!this.pendingMap.has(req.id)) return;
      clearTimeout(timer);
      this.pendingMap.delete(req.id);
      this.emit("resolved", req, yes, reason);
      settle(yes);
    };
    this.pendingMap.set(req.id, { req, done: (yes) => finish(yes, yes ? "approved" : "denied") });
    this.emit("request", req);
    return { req, result, cancel: () => finish(false, "disconnected") };
  }

  answer(id: string, yes: boolean): boolean {
    const p = this.pendingMap.get(id);
    if (!p) return false;
    p.done(yes);
    return true;
  }

  pending(): PairRequest[] {
    return [...this.pendingMap.values()].map((p) => p.req);
  }
}

export function formatCode(code: string): string {
  return `${code.slice(0, 3)} ${code.slice(3)}`;
}
