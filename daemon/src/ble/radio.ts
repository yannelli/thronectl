import { fork, type ChildProcess } from "node:child_process";
import { existsSync, readdirSync } from "node:fs";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import { logger } from "../log.js";
import type { Advert, Radio } from "./manager.js";
import { parseAdvert } from "./manager.js";
import type { BleLink } from "./pipe.js";
import type { FromWorker, ToWorker } from "./worker.js";

const log = logger("ble");
const READY_MS = 15_000;
// The helper splits these into MTU-sized packets, so the pipe can hand it big pieces.
const PIECE = 16 * 1024;

/** Why Bluetooth cannot run here, or undefined when it may. Cheap checks only; the helper finds the rest. */
export function bluetoothUnavailable(): string | undefined {
  if (process.env.THRONECTL_BLE_FAKE) return undefined;
  try { createRequire(import.meta.url).resolve("@stoprocent/noble"); } catch { return "the @stoprocent/noble module is not installed"; }
  if (process.platform === "linux") {
    const dir = "/sys/class/bluetooth";
    if (!existsSync(dir) || !readdirSync(dir).some((n) => n.startsWith("hci"))) return "this host has no Bluetooth adapter";
  } else if (process.platform !== "darwin") {
    return `Bluetooth is not supported on ${process.platform}`;
  }
  return undefined;
}

/** Explains a helper exit, so the log says what to do. */
function exitReason(code: number | null, signal: NodeJS.Signals | null): string {
  if (process.platform === "darwin" && (signal === "SIGABRT" || signal === "SIGKILL"))
    return "macOS stopped the Bluetooth helper: allow Bluetooth for the app that runs thronectl (System Settings, Privacy & Security, Bluetooth)";
  return `the Bluetooth helper exited (${signal ?? `code ${code}`})`;
}

class WorkerRadio implements Radio {
  private child?: ChildProcess;
  private onAdvert: (a: Advert) => void = () => {};
  private links = new Map<string, { data?: (b: Buffer) => void; close: (() => void)[]; pending: Buffer[]; bytes: number }>();
  private connects = new Map<string, { resolve: (mtu: number) => void; reject: (e: Error) => void }>();
  private writes = new Map<number, { resolve: () => void; reject: (e: Error) => void }>();
  private seq = 1;

  async start(onAdvert: (a: Advert) => void): Promise<void> {
    this.onAdvert = onAdvert;
    const child = fork(fileURLToPath(new URL("./worker.js", import.meta.url)), [], { serialization: "advanced", stdio: ["ignore", "inherit", "inherit", "ipc"] });
    this.child = child;
    const ready = new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("the Bluetooth helper did not start")), READY_MS);
      let up = false;
      child.on("message", (m: FromWorker) => {
        if (m.t !== "state") return this.onMessage(m);
        if (m.state === "poweredOn") { up = true; clearTimeout(timer); resolve(); return; }
        if (m.state === "unknown" || m.state === "resetting") return;
        if (!up) { clearTimeout(timer); reject(new Error(`Bluetooth is ${m.state}`)); return; }
        log.warn(`Bluetooth is ${m.state}`);
        this.dropAll(`Bluetooth is ${m.state}`);
      });
      child.once("exit", (code, signal) => { clearTimeout(timer); reject(new Error(exitReason(code, signal))); });
    });
    child.on("exit", (code, signal) => {
      if (this.child !== child) return;
      this.child = undefined;
      log.warn(exitReason(code, signal));
      this.dropAll("the Bluetooth helper stopped");
    });
    this.post({ t: "start" });
    await ready.catch((e: Error) => { void this.stop(); throw e; });
  }

  async stop(): Promise<void> {
    const c = this.child;
    this.child = undefined;
    this.dropAll("Bluetooth stopped");
    c?.kill();
  }

  connect(handle: string): Promise<BleLink> {
    this.links.set(handle, { close: [], pending: [], bytes: 0 });
    return new Promise<number>((resolve, reject) => {
      this.connects.set(handle, { resolve, reject });
      if (!this.post({ t: "connect", handle })) {
        this.connects.delete(handle);
        this.links.delete(handle);
        reject(new Error("the Bluetooth helper stopped"));
      }
    }).then((mtu) => this.link(handle, mtu));
  }

  private link(handle: string, mtu: number): BleLink {
    const ends = this.links.get(handle);
    if (!ends) throw new Error("Bluetooth disconnected during setup");
    log.debug(`ATT MTU ${mtu}`);
    return {
      mtu: PIECE,
      write: (data) => new Promise<void>((resolve, reject) => {
        const seq = this.seq++;
        this.writes.set(seq, { resolve, reject });
        if (!this.post({ t: "write", handle, seq, data })) this.settle(seq, "the Bluetooth helper stopped");
      }),
      onData: (cb) => {
        ends.data = cb;
        for (const b of ends.pending.splice(0)) cb(b);
        ends.bytes = 0;
      },
      onClose: (cb) => { ends.close.push(cb); },
      close: () => { this.post({ t: "close", handle }); },
    };
  }

  private onMessage(m: FromWorker): void {
    switch (m.t) {
      case "advert": {
        const a = parseAdvert(m.handle, m.data ? Buffer.from(m.data) : undefined, m.rssi);
        if (a) this.onAdvert(a);
        break;
      }
      case "connected": this.connects.get(m.handle)?.resolve(m.mtu); this.connects.delete(m.handle); break;
      case "connectError":
        this.connects.get(m.handle)?.reject(new Error(m.error));
        this.connects.delete(m.handle);
        this.links.delete(m.handle);
        break;
      case "written": this.settle(m.seq, m.error); break;
      case "data": {
        const ends = this.links.get(m.handle);
        if (!ends) break;
        const data = Buffer.from(m.data);
        if (ends.data) ends.data(data);
        else if (ends.bytes + data.length <= 64 * 1024) {
          ends.pending.push(data);
          ends.bytes += data.length;
        } else this.post({ t: "close", handle: m.handle });
        break;
      }
      case "closed": this.closed(m.handle); break;
      default: break;
    }
  }

  private settle(seq: number, error?: string): void {
    const w = this.writes.get(seq);
    this.writes.delete(seq);
    if (error) w?.reject(new Error(error));
    else w?.resolve();
  }

  private closed(handle: string): void {
    this.connects.get(handle)?.reject(new Error("Bluetooth disconnected during setup"));
    this.connects.delete(handle);
    const l = this.links.get(handle);
    this.links.delete(handle);
    for (const cb of l?.close ?? []) cb();
  }

  private dropAll(why: string): void {
    for (const [h, c] of this.connects) { c.reject(new Error(why)); this.connects.delete(h); }
    for (const seq of [...this.writes.keys()]) this.settle(seq, why);
    for (const h of [...this.links.keys()]) this.closed(h);
  }

  private post(m: ToWorker): boolean {
    if (!this.child?.connected) return false;
    this.child.send(m);
    return true;
  }
}

/** Loads the BLE backend for this host. The daemon runs without it. */
export async function loadRadio(): Promise<{ radio: Radio } | { error: string }> {
  const why = bluetoothUnavailable();
  return why ? { error: why } : { radio: new WorkerRadio() };
}
