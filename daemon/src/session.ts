import { EventEmitter } from "node:events";
import type { Duplex } from "node:stream";
import { deriveKeys, generateKeyPair, keyId, open, random, seal, sha256, x25519, type KeyPair, type SessionKeys } from "./crypto.js";
import { FrameReader, writeFrame } from "./framing.js";

export const KIND_JSON = 0x01;
export const KIND_MIC = 0x02;
export const KIND_SPK = 0x03;
export const KIND_OTA = 0x04;

/** The byte stream under a session: a TCP socket, or the Bluetooth pipe (remoteAddress names the device). */
export type Stream = Duplex & { remoteAddress?: string };

/** A framed connection. Frames queue until a reader calls next() or setHandler(). */
export class Conn {
  private queue: Buffer[] = [];
  private waiters: ((frame: Buffer | null) => void)[] = [];
  private handler?: (frame: Buffer) => void;
  closed = false;

  constructor(readonly sock: Stream) {
    const reader = new FrameReader((f) => this.deliver(f), (e) => sock.destroy(e));
    sock.on("data", (chunk: Buffer) => reader.push(chunk));
    sock.on("close", () => {
      this.closed = true;
      for (const w of this.waiters.splice(0)) w(null);
    });
    sock.on("error", () => { /* close follows */ });
  }

  private deliver(frame: Buffer): void {
    if (this.handler) this.handler(frame);
    else if (this.waiters.length) this.waiters.shift()!(frame);
    else this.queue.push(frame);
  }

  next(timeoutMs: number): Promise<Buffer> {
    const queued = this.queue.shift();
    if (queued) return Promise.resolve(queued);
    if (this.closed) return Promise.reject(new Error("connection closed"));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.waiters = this.waiters.filter((w) => w !== waiter);
        reject(new Error("handshake timeout"));
      }, timeoutMs);
      const waiter = (f: Buffer | null): void => {
        clearTimeout(timer);
        if (f) resolve(f); else reject(new Error("connection closed"));
      };
      this.waiters.push(waiter);
    });
  }

  setHandler(h: (frame: Buffer) => void): void {
    this.handler = h;
    for (const f of this.queue.splice(0)) h(f);
  }

  send(payload: Buffer): boolean {
    if (this.closed) return false;
    return writeFrame(this.sock, payload);
  }

  sendJson(obj: unknown): void {
    this.send(Buffer.from(JSON.stringify(obj)));
  }
}

/** AES-256-GCM channel over a Conn, per docs/protocol.md. */
export class SecureChannel extends EventEmitter {
  private tx = 0n;
  private rx = 0n;
  lastRx = Date.now();

  constructor(private readonly conn: Conn, private readonly txKey: Buffer, private readonly rxKey: Buffer) {
    super();
    conn.setHandler((f) => this.onFrame(f));
    if (conn.closed) queueMicrotask(() => this.emit("close"));
    else conn.sock.on("close", () => this.emit("close"));
  }

  get closed(): boolean {
    return this.conn.closed;
  }

  get remote(): string {
    return this.conn.sock.remoteAddress?.replace(/^::ffff:/, "") ?? "?";
  }

  private onFrame(frame: Buffer): void {
    let plain: Buffer;
    try {
      plain = open(this.rxKey, this.rx++, frame);
    } catch {
      this.emit("error", new Error("decrypt failed"));
      this.close();
      return;
    }
    this.lastRx = Date.now();
    const kind = plain[0];
    const body = plain.subarray(1);
    if (kind === KIND_JSON) {
      let msg: unknown;
      try { msg = JSON.parse(body.toString("utf8")); } catch { return; }
      this.emit("json", msg);
    } else {
      this.emit("audio", body, kind);
    }
  }

  sendRaw(kind: number, body: Buffer): boolean {
    if (this.conn.closed) return false;
    const plain = Buffer.concat([Buffer.from([kind]), body]);
    return this.conn.send(seal(this.txKey, this.tx++, plain));
  }

  sendJson(obj: unknown): boolean {
    return this.sendRaw(KIND_JSON, Buffer.from(JSON.stringify(obj), "utf8"));
  }

  get writableLength(): number {
    return this.conn.sock.writableLength;
  }

  close(): void {
    this.conn.sock.end();
    setTimeout(() => this.conn.sock.destroy(), 500).unref();
  }
}

function b64(v: unknown, len: number, field: string): Buffer {
  if (typeof v !== "string") throw new Error(`missing ${field}`);
  const b = Buffer.from(v, "base64");
  if (b.length !== len) throw new Error(`bad ${field} length`);
  return b;
}

function parse(frame: Buffer): Record<string, unknown> {
  const v = JSON.parse(frame.toString("utf8")) as unknown;
  if (typeof v !== "object" || v === null) throw new Error("bad handshake frame");
  return v as Record<string, unknown>;
}

const clip = (v: unknown, n: number): string => (typeof v === "string" ? v.slice(0, n) : "");

export interface DevicePeer {
  id: string;
  name: string;
  fw: string;
  pk: Buffer;
  known: boolean;
}

export interface ServerHandshake {
  channel: SecureChannel;
  device: DevicePeer;
  keys: SessionKeys;
}

export async function serverHandshake(conn: Conn, host: KeyPair, hostId: string, hostName: string): Promise<ServerHandshake> {
  const hello = parse(await conn.next(10_000));
  if (hello.t !== "hello" || hello.v !== 1) throw new Error("expected hello v1");
  const dpk = b64(hello.pk, 32, "pk");
  const depk = b64(hello.epk, 32, "epk");
  const commit = b64(hello.c, 32, "c");
  const eph = generateKeyPair();
  const hn = random(16);
  conn.sendJson({ t: "hello", v: 1, id: hostId, name: hostName, pk: host.pub.toString("base64"), epk: eph.pub.toString("base64"), n: hn.toString("base64") });
  const reveal = parse(await conn.next(15_000));
  if (reveal.t !== "reveal") throw new Error("expected reveal");
  const dn = b64(reveal.n, 16, "n");
  if (!sha256(dn).equals(commit)) throw new Error("commitment mismatch");
  const keys = deriveKeys({ dpk, depk, dn, hpk: host.pub, hepk: eph.pub, hn, ss: x25519(host, dpk), ee: x25519(eph, depk) });
  const channel = new SecureChannel(conn, keys.hd, keys.dh);
  const device: DevicePeer = { id: keyId(dpk), name: clip(hello.name, 32) || "Cardputer", fw: clip(hello.fw, 16), pk: dpk, known: reveal.known === true };
  return { channel, device, keys };
}

export interface HostPeer {
  id: string;
  name: string;
  pk: Buffer;
}

export interface ClientHandshake {
  channel: SecureChannel;
  host: HostPeer;
  keys: SessionKeys;
}

/** Device side of the handshake. Used by the simulator and tests. */
export async function clientHandshake(
  conn: Conn, device: KeyPair, name: string, fw: string, isKnown: (host: HostPeer) => boolean,
): Promise<ClientHandshake> {
  const eph = generateKeyPair();
  const dn = random(16);
  conn.sendJson({ t: "hello", v: 1, name, fw, pk: device.pub.toString("base64"), epk: eph.pub.toString("base64"), c: sha256(dn).toString("base64") });
  const hello = parse(await conn.next(10_000));
  if (hello.t !== "hello" || hello.v !== 1) throw new Error("expected host hello v1");
  const hpk = b64(hello.pk, 32, "pk");
  const hepk = b64(hello.epk, 32, "epk");
  const hn = b64(hello.n, 16, "n");
  const host: HostPeer = { id: keyId(hpk), name: clip(hello.name, 64), pk: hpk };
  conn.sendJson({ t: "reveal", n: dn.toString("base64"), known: isKnown(host) });
  const keys = deriveKeys({ dpk: device.pub, depk: eph.pub, dn, hpk, hepk, hn, ss: x25519(device, hpk), ee: x25519(eph, hepk) });
  return { channel: new SecureChannel(conn, keys.dh, keys.hd), host, keys };
}
