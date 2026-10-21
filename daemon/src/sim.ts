import { connect } from "node:net";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { generateKeyPair, type KeyPair } from "./crypto.js";
import { configDir, VERSION } from "./paths.js";
import { clientHandshake, Conn, type HostPeer, type SecureChannel } from "./session.js";

/** A software device: same protocol as the firmware. Used by `thronectl sim` and the e2e tests. */
export class SimDevice {
  private nextId = 1;
  private waiting = new Map<number, { resolve: (r: unknown) => void; reject: (e: Error) => void }>();
  ch?: SecureChannel;
  host?: HostPeer;
  onEvent: (e: string, d: unknown) => void = () => {};
  onAuth: (state: string, info: Record<string, unknown>) => void = () => {};

  constructor(readonly keys: KeyPair, readonly name: string, readonly knownHosts: Map<string, string>) {}

  static load(dir = configDir()): SimDevice {
    const file = join(dir, "sim-device.json");
    let data: { priv: string; pub: string; hosts: Record<string, string> };
    if (existsSync(file)) data = JSON.parse(readFileSync(file, "utf8"));
    else {
      const kp = generateKeyPair();
      data = { priv: kp.priv.toString("base64"), pub: kp.pub.toString("base64"), hosts: {} };
      writeFileSync(file, JSON.stringify(data), { mode: 0o600 });
    }
    const dev = new SimDevice({ priv: Buffer.from(data.priv, "base64"), pub: Buffer.from(data.pub, "base64") }, "Simulator", new Map(Object.entries(data.hosts)));
    dev.save = () => writeFileSync(file, JSON.stringify({ ...data, hosts: Object.fromEntries(dev.knownHosts) }), { mode: 0o600 });
    return dev;
  }

  save: () => void = () => {};

  /** Connects, runs the handshake, and resolves once the host says auth ok. */
  async connect(host: string, port: number): Promise<HostPeer> {
    const sock = connect(port, host);
    await new Promise<void>((res, rej) => { sock.once("connect", res); sock.once("error", rej); });
    const conn = new Conn(sock);
    const hs = await clientHandshake(conn, this.keys, this.name, VERSION, (h) => this.knownHosts.get(h.id) === h.pk.toString("base64"));
    const pinned = this.knownHosts.get(hs.host.id);
    if (pinned && pinned !== hs.host.pk.toString("base64")) throw new Error("host key changed");
    this.ch = hs.channel;
    this.host = hs.host;
    return new Promise<HostPeer>((resolve, reject) => {
      hs.channel.on("close", () => reject(new Error("connection closed")));
      hs.channel.on("json", (m: Record<string, unknown>) => {
        if (m.t === "auth") {
          this.onAuth(String(m.state), m);
          if (m.state === "ok") {
            this.knownHosts.set(hs.host.id, hs.host.pk.toString("base64"));
            this.save();
            resolve(hs.host);
          } else if (m.state === "denied") reject(new Error("pairing denied"));
        } else if (m.t === "res") {
          const w = this.waiting.get(m.id as number);
          if (!w) return;
          this.waiting.delete(m.id as number);
          if (m.ok) w.resolve(m.r); else w.reject(new Error(String(m.err)));
        } else if (m.t === "ev") {
          this.onEvent(String(m.e), m.d);
        }
      });
    });
  }

  call<T = Record<string, unknown>>(m: string, p: Record<string, unknown> = {}, timeoutMs = 30_000): Promise<T> {
    if (!this.ch) return Promise.reject(new Error("not connected"));
    const id = this.nextId++;
    return new Promise<T>((resolve, reject) => {
      const timer = setTimeout(() => { this.waiting.delete(id); reject(new Error(`${m} timed out`)); }, timeoutMs);
      this.waiting.set(id, {
        resolve: (r) => { clearTimeout(timer); resolve(r as T); },
        reject: (e) => { clearTimeout(timer); reject(e); },
      });
      this.ch!.sendJson({ t: "req", id, m, p });
    });
  }

  close(): void {
    this.ch?.close();
  }
}
