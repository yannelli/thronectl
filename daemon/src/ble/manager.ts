import { blePipe, type BleLink } from "./pipe.js";
import type { Stream } from "../session.js";
import { logger } from "../log.js";
import { BLE_COMPANY, BLE_FLAG_PAIRING } from "./uuids.js";

export { BLE_COMPANY, BLE_FLAG_PAIRING } from "./uuids.js";

const log = logger("ble");

export interface Advert {
  handle: string;  // the radio's name for the peripheral; macOS hides the MAC
  id: string;      // device key id, 16 hex chars
  pairing: boolean;
  rssi: number;
}

/** What the manager needs from a BLE library. */
export interface Radio {
  start(onAdvert: (a: Advert) => void): Promise<void>;
  stop(): Promise<void>;
  connect(handle: string): Promise<BleLink>;
}

/** Parses the manufacturer data of a thronectl advertisement, or undefined. */
export function parseAdvert(handle: string, data: Buffer | undefined, rssi: number): Advert | undefined {
  if (!data || data.length < 13 || data.readUInt16LE(0) !== BLE_COMPANY || data.toString("latin1", 2, 4) !== "TC") return undefined;
  return { handle, id: data.subarray(4, 12).toString("hex"), pairing: (data[12] & BLE_FLAG_PAIRING) !== 0, rssi };
}

export interface ManagerHooks {
  known(id: string): boolean;
  connected(id: string): boolean;  // already linked, over Wi-Fi or Bluetooth
  serve(stream: Stream, addr: string): Promise<void>;
}

const RETRY_MS = 10_000;
const MIN_RSSI = -95;

/** Connects to nearby devices: paired ones at once, unknown ones only while they ask to pair. */
export class BleManager {
  private busy = new Set<string>();
  private retryAt = new Map<string, number>();
  private running = false;

  constructor(private readonly radio: Radio, private readonly hooks: ManagerHooks) {}

  async start(): Promise<void> {
    this.running = true;
    await this.radio.start((a) => this.onAdvert(a));
  }

  async stop(): Promise<void> {
    this.running = false;
    await this.radio.stop();
  }

  private onAdvert(a: Advert): void {
    if (!this.running || this.busy.has(a.id) || a.rssi < MIN_RSSI) return;
    if (!a.pairing && !this.hooks.known(a.id)) return;
    if (this.hooks.connected(a.id) || Date.now() < (this.retryAt.get(a.id) ?? 0)) return;
    this.busy.add(a.id);
    void this.link(a).finally(() => this.busy.delete(a.id));
  }

  private async link(a: Advert): Promise<void> {
    let link: BleLink;
    try {
      link = await this.radio.connect(a.handle);
    } catch (e) {
      log.warn(`could not connect to device ${a.id}`, e);
      this.retryAt.set(a.id, Date.now() + RETRY_MS);
      return;
    }
    log.info(`connected to device ${a.id} over Bluetooth (rssi ${a.rssi})`);
    const stream = blePipe(link, "Bluetooth");
    const closed = new Promise<void>((r) => stream.once("close", () => r()));
    await this.hooks.serve(stream, "Bluetooth");
    await closed;
    this.retryAt.set(a.id, Date.now() + 2000);
  }
}
