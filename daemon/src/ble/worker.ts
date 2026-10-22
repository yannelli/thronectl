import { createRequire } from "node:module";
import { pathToFileURL } from "node:url";
import { BLE_CREDIT, BLE_RX, BLE_SERVICE, BLE_TX } from "./uuids.js";

/** The Bluetooth helper process: it owns noble, so a crash or a macOS permission kill cannot stop the daemon. */

export type ToWorker =
  | { t: "start" }
  | { t: "connect"; handle: string }
  | { t: "write"; handle: string; seq: number; data: Buffer }
  | { t: "close"; handle: string };

export type FromWorker =
  | { t: "state"; state: string }
  | { t: "advert"; handle: string; data?: Buffer; rssi: number }
  | { t: "connected"; handle: string; mtu: number }
  | { t: "connectError"; handle: string; error: string }
  | { t: "written"; handle: string; seq: number; error?: string }
  | { t: "data"; handle: string; data: Buffer }
  | { t: "closed"; handle: string };

/* eslint-disable @typescript-eslint/no-explicit-any */
type Noble = any;
type Peripheral = any;
type Characteristic = any;

const CONNECT_MS = 10_000;
// Bytes in flight before the device reports what it has read. CoreBluetooth drops writes when its queue fills.
const WINDOW = process.platform === "darwin" ? 4096 : 12_288;
const id = (uuid: string): string => uuid.replace(/-/g, "").toLowerCase();

interface Conn {
  p: Peripheral;
  rx: Characteristic;
  mtu: number;
  sent: number;
  consumed: number;
  queue: { seq: number; data: Buffer }[];
  pumping: boolean;
  credit?: () => void;
}

const send = (m: FromWorker): void => { process.send?.(m); };
const conns = new Map<string, Conn>();
const seen = new Map<string, Peripheral>();
let noble: Noble;

async function load(): Promise<Noble> {
  // Tests swap in a fake radio module.
  if (process.env.THRONECTL_BLE_FAKE) return (await import(pathToFileURL(process.env.THRONECTL_BLE_FAKE).href)).default;
  const require = createRequire(import.meta.url);
  if (process.platform === "linux" && !process.env.NOBLE_BINDINGS) {
    try { require.resolve("dbus-next"); process.env.NOBLE_BINDINGS = "dbus"; } catch { /* raw HCI */ }
  }
  return require("@stoprocent/noble");
}

function withTimeout<T>(p: Promise<T>, ms: number, onTimeout: () => void): Promise<T> {
  return new Promise((resolve, reject) => {
    const t = setTimeout(() => { onTimeout(); reject(new Error("timed out")); }, ms);
    p.then((v) => { clearTimeout(t); resolve(v); }, (e) => { clearTimeout(t); reject(e); });
  });
}

async function start(): Promise<void> {
  noble = await load();
  noble.on("stateChange", (state: string) => send({ t: "state", state }));
  await noble.waitForPoweredOnAsync(10_000).catch(() => undefined);
  send({ t: "state", state: noble.state });
  if (noble.state !== "poweredOn") return;
  noble.on("discover", (p: Peripheral) => {
    seen.set(p.id, p);
    send({ t: "advert", handle: p.id, data: p.advertisement?.manufacturerData, rssi: p.rssi ?? -127 });
  });
  await noble.startScanningAsync([id(BLE_SERVICE)], true);
}

async function rescan(): Promise<void> {
  await noble.stopScanningAsync().catch(() => undefined);
  await noble.startScanningAsync([id(BLE_SERVICE)], true).catch(() => undefined);
}

/** The credit value: u32 bytes the device has read from its buffer, then u16 ATT MTU. */
function readCredit(buf: Buffer, c: Conn): void {
  if (buf.length < 4) return;
  c.consumed = buf.readUInt32LE(0);
  if (buf.length >= 6 && buf.readUInt16LE(4) > 23) c.mtu = buf.readUInt16LE(4);
  c.credit?.();
}

async function connect(handle: string): Promise<void> {
  const p = seen.get(handle);
  if (!p) throw new Error("device is no longer in range");
  await withTimeout(p.connectAsync(), CONNECT_MS, () => p.cancelConnect?.());
  const { characteristics: cs } = await withTimeout<{ characteristics: Characteristic[] }>(
    p.discoverSomeServicesAndCharacteristicsAsync([id(BLE_SERVICE)], [id(BLE_RX), id(BLE_TX), id(BLE_CREDIT)]),
    CONNECT_MS, () => p.disconnect?.());
  const find = (u: string): Characteristic => {
    const c = cs.find((x: Characteristic) => x.uuid === id(u));
    if (!c) throw new Error(`the device has no characteristic ${u}`);
    return c;
  };
  const rx = find(BLE_RX), tx = find(BLE_TX), cr = find(BLE_CREDIT);
  const c: Conn = { p, rx, mtu: 23, sent: 0, consumed: 0, queue: [], pumping: false };
  readCredit(await withTimeout<Buffer>(cr.readAsync(), CONNECT_MS, () => p.disconnect?.()), c);
  let disconnected = false, connected = false;
  let early = Buffer.alloc(0);
  const onCredit = (b: Buffer): void => readCredit(b, c);
  const onData = (b: Buffer): void => {
    if (connected) return send({ t: "data", handle, data: b });
    if (early.length + b.length > 64 * 1024) { p.disconnect?.(); return; }
    early = Buffer.concat([early, b]);
  };
  cr.on("data", onCredit);
  tx.on("data", onData);
  p.once("disconnect", () => {
    disconnected = true;
    cr.removeListener("data", onCredit);
    tx.removeListener("data", onData);
    if (conns.get(handle) !== c) return;
    conns.delete(handle);
    for (const q of c.queue) send({ t: "written", handle, seq: q.seq, error: "disconnected" });
    c.credit?.();
    send({ t: "closed", handle });
    void rescan();
  });
  await withTimeout(cr.subscribeAsync(), CONNECT_MS, () => p.disconnect?.());
  await withTimeout(tx.subscribeAsync(), CONNECT_MS, () => p.disconnect?.());
  if (disconnected) throw new Error("disconnected during setup");
  conns.set(handle, c);
  send({ t: "connected", handle, mtu: c.mtu });
  connected = true;
  if (early.length) send({ t: "data", handle, data: early });
  early = Buffer.alloc(0);
}

/** Sends queued writes in MTU-3 packets, keeping at most WINDOW bytes unread on the device. */
async function pump(handle: string, c: Conn): Promise<void> {
  if (c.pumping) return;
  c.pumping = true;
  try {
    while (c.queue.length && conns.get(handle) === c) {
      const q = c.queue[0];
      for (let off = 0; off < q.data.length;) {
        if (conns.get(handle) !== c) return;
        const room = WINDOW - ((c.sent - c.consumed) >>> 0);
        if (room <= 0) {
          await new Promise<void>((r) => { c.credit = r; setTimeout(r, 2000); });
          c.credit = undefined;
          continue;
        }
        const n = Math.min(c.mtu - 3, room, q.data.length - off);
        await c.rx.writeAsync(q.data.subarray(off, off + n), true);
        c.sent = (c.sent + n) >>> 0;
        off += n;
      }
      c.queue.shift();
      send({ t: "written", handle, seq: q.seq });
    }
  } catch (e) {
    const err = e instanceof Error ? e.message : String(e);
    for (const q of c.queue.splice(0)) send({ t: "written", handle, seq: q.seq, error: err });
    c.p.disconnect?.();
  } finally {
    c.pumping = false;
  }
}

process.on("message", (m: ToWorker) => {
  switch (m.t) {
    case "start":
      start().catch((e: Error) => send({ t: "state", state: `error: ${e.message}` }));
      break;
    case "connect":
      connect(m.handle).catch((e: Error) => {
        seen.get(m.handle)?.disconnect?.();
        send({ t: "connectError", handle: m.handle, error: e.message });
      });
      break;
    case "write": {
      const c = conns.get(m.handle);
      if (!c) { send({ t: "written", handle: m.handle, seq: m.seq, error: "not connected" }); break; }
      c.queue.push({ seq: m.seq, data: Buffer.from(m.data) });
      void pump(m.handle, c);
      break;
    }
    case "close":
      conns.get(m.handle)?.p.disconnect?.();
      break;
  }
});
process.on("disconnect", () => process.exit(0));
process.title = "thronectl-ble";
