import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { generateKeyPair, keyId, type KeyPair } from "./crypto.js";
import { paths } from "./paths.js";

export interface HostIdentity extends KeyPair {
  id: string;
}

export function loadIdentity(): HostIdentity {
  const file = paths.identity();
  if (existsSync(file)) {
    const raw = JSON.parse(readFileSync(file, "utf8")) as { priv: string; pub: string };
    const kp = { priv: Buffer.from(raw.priv, "base64"), pub: Buffer.from(raw.pub, "base64") };
    return { ...kp, id: keyId(kp.pub) };
  }
  const kp = generateKeyPair();
  writeFileSync(file, JSON.stringify({ priv: kp.priv.toString("base64"), pub: kp.pub.toString("base64") }) + "\n", { mode: 0o600 });
  return { ...kp, id: keyId(kp.pub) };
}

export interface TrustedDevice {
  id: string;
  name: string;
  pk: string; // base64 static public key
  pairedAt: string;
  lastSeen?: string;
  lastAddr?: string;
  fw?: string;
}

export class DeviceStore {
  private devices: TrustedDevice[] = [];

  constructor(private readonly file: string = paths.devices()) {
    this.reload();
  }

  reload(): void {
    this.devices = existsSync(this.file) ? (JSON.parse(readFileSync(this.file, "utf8")) as TrustedDevice[]) : [];
  }

  list(): TrustedDevice[] {
    return [...this.devices];
  }

  findByKey(pk: Buffer): TrustedDevice | undefined {
    const b64 = pk.toString("base64");
    return this.devices.find((d) => d.pk === b64);
  }

  add(device: TrustedDevice): void {
    this.devices = this.devices.filter((d) => d.id !== device.id);
    this.devices.push(device);
    this.save();
  }

  touch(id: string, addr: string, fw: string): void {
    const d = this.devices.find((x) => x.id === id);
    if (!d) return;
    d.lastSeen = new Date().toISOString();
    d.lastAddr = addr;
    d.fw = fw;
    this.save();
  }

  /** Removes by exact ID, ID prefix, or name. Returns the removed devices. */
  remove(match: string): TrustedDevice[] {
    const hit = (d: TrustedDevice): boolean => d.id.startsWith(match) || d.name === match;
    const removed = this.devices.filter(hit);
    this.devices = this.devices.filter((d) => !hit(d));
    this.save();
    return removed;
  }

  private save(): void {
    writeFileSync(this.file, JSON.stringify(this.devices, null, 2) + "\n", { mode: 0o600 });
  }
}
