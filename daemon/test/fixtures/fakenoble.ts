import { EventEmitter } from "node:events";
import { connect, type Socket } from "node:net";

/** A stand-in for @stoprocent/noble. Its one peripheral is a TCP link to the test, which plays the device. */
// Socket frames: u8 type, u16 LE length, payload. 1 = host write to RX, 2 = credit value, 3 = TX notification.
const port = Number(process.env.THRONECTL_BLE_FAKE_PORT);
const advert = Buffer.from(process.env.THRONECTL_BLE_FAKE_ADV ?? "", "hex");

function frame(type: number, payload: Buffer): Buffer {
  const h = Buffer.alloc(3);
  h[0] = type;
  h.writeUInt16LE(payload.length, 1);
  return Buffer.concat([h, payload]);
}

class Char extends EventEmitter {
  value = Buffer.alloc(0);
  subscribed = false;
  pending: Buffer[] = [];
  notify(b: Buffer): void {
    if (this.subscribed) this.emit("data", b, true);
    else this.pending.push(b);
  }
  constructor(readonly uuid: string, private readonly onWrite?: (b: Buffer) => void) { super(); }
  async readAsync(): Promise<Buffer> { return this.value; }
  async subscribeAsync(): Promise<void> {
    if (this.listenerCount("data") !== 1) throw new Error("notification listeners were not cleaned up");
    this.subscribed = true;
    for (const b of this.pending.splice(0)) this.emit("data", b, true);
    await new Promise((resolve) => setTimeout(resolve, 50));
  }
  async writeAsync(b: Buffer, withoutResponse: boolean): Promise<void> {
    if (!withoutResponse) throw new Error("expected write without response");
    if (b.length > 244) throw new Error(`write of ${b.length} bytes is over MTU-3`);
    this.onWrite?.(b);
  }
}

class Peripheral extends EventEmitter {
  id = "fake-peripheral";
  rssi = -55;
  advertisement = { manufacturerData: advert };
  private sock?: Socket;
  private rx = new Char("7b3e00026f1a4c2e9d6a2f1c8e5a7d10", (b) => this.sock?.write(frame(1, b)));
  private tx = new Char("7b3e00036f1a4c2e9d6a2f1c8e5a7d10");
  private cr = new Char("7b3e00046f1a4c2e9d6a2f1c8e5a7d10");

  async connectAsync(): Promise<void> {
    const sock = connect(port, "127.0.0.1");
    await new Promise<void>((res, rej) => { sock.once("connect", res); sock.once("error", rej); });
    this.sock = sock;
    this.cr.subscribed = this.tx.subscribed = false;
    this.cr.pending = []; this.tx.pending = [];
    const initial = Buffer.alloc(6);
    initial.writeUInt16LE(247, 4);
    this.cr.value = initial;
    let buf = Buffer.alloc(0);
    sock.on("data", (d: Buffer) => {
      buf = Buffer.concat([buf, d]);
      while (buf.length >= 3 && buf.length >= 3 + buf.readUInt16LE(1)) {
        const type = buf[0], body = buf.subarray(3, 3 + buf.readUInt16LE(1));
        buf = buf.subarray(3 + body.length);
        if (type === 2) { this.cr.value = Buffer.from(body); this.cr.notify(this.cr.value); }
        if (type === 3) this.tx.notify(Buffer.from(body));
      }
    });
    sock.on("close", () => this.emit("disconnect"));
  }

  cancelConnect(): void {}
  disconnect(): void { this.sock?.destroy(); }

  async discoverSomeServicesAndCharacteristicsAsync(): Promise<{ characteristics: Char[] }> {
    return { characteristics: [this.rx, this.tx, this.cr] };
  }
}

class FakeNoble extends EventEmitter {
  state = "poweredOn";
  private timer?: NodeJS.Timeout;
  private peripheral = new Peripheral();
  async waitForPoweredOnAsync(): Promise<void> {}
  async startScanningAsync(): Promise<void> {
    const p = this.peripheral;
    clearInterval(this.timer);
    this.timer = setInterval(() => this.emit("discover", p), 200);
  }
  async stopScanningAsync(): Promise<void> { clearInterval(this.timer); }
}

export default new FakeNoble();
