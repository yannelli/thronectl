import { test } from "node:test";
import assert from "node:assert/strict";
import { BleManager, BLE_COMPANY, parseAdvert, type Advert, type Radio } from "../src/ble/manager.js";
import { blePipe, fakeRadio, type BleLink } from "../src/ble/pipe.js";

const tick = (): Promise<void> => new Promise((r) => setTimeout(r, 20));

function advertData(id: string, flags: number): Buffer {
  const b = Buffer.alloc(13);
  b.writeUInt16LE(BLE_COMPANY, 0);
  b.write("TC", 2, "latin1");
  Buffer.from(id, "hex").copy(b, 4);
  b[12] = flags;
  return b;
}

class FakeRadio implements Radio {
  emit: (a: Advert) => void = () => {};
  connects: string[] = [];
  fail = false;
  peers: BleLink[] = [];
  async start(cb: (a: Advert) => void): Promise<void> { this.emit = cb; }
  async stop(): Promise<void> {}
  async connect(handle: string): Promise<BleLink> {
    this.connects.push(handle);
    if (this.fail) throw new Error("no answer");
    const [host, dev] = fakeRadio(20);
    this.peers.push(dev);
    return host;
  }
}

test("parses thronectl adverts and ignores others", () => {
  const a = parseAdvert("h1", advertData("0011223344556677", 1), -60);
  assert.deepEqual(a, { handle: "h1", id: "0011223344556677", pairing: true, rssi: -60 });
  assert.equal(parseAdvert("h2", Buffer.from([0x4c, 0x00, 1, 2]), -60), undefined);
  assert.equal(parseAdvert("h3", undefined, -60), undefined);
});

test("connects to paired devices, and to unknown ones only in pairing mode", async () => {
  const radio = new FakeRadio();
  const served: string[] = [];
  const mgr = new BleManager(radio, {
    known: (id) => id === "aaaaaaaaaaaaaaaa",
    connected: () => false,
    serve: async (s, addr) => { served.push(addr); },
  });
  await mgr.start();
  radio.emit({ handle: "p1", id: "aaaaaaaaaaaaaaaa", pairing: false, rssi: -50 });
  radio.emit({ handle: "p1", id: "aaaaaaaaaaaaaaaa", pairing: false, rssi: -50 });
  radio.emit({ handle: "p2", id: "bbbbbbbbbbbbbbbb", pairing: false, rssi: -50 });
  radio.emit({ handle: "p4", id: "dddddddddddddddd", pairing: true, rssi: -99 });
  await tick();
  assert.deepEqual(radio.connects, ["p1"], "one link per device; strangers and weak signals are skipped");
  radio.emit({ handle: "p3", id: "cccccccccccccccc", pairing: true, rssi: -70 });
  await tick();
  assert.deepEqual(radio.connects, ["p1", "p3"]);
  assert.deepEqual(served, ["Bluetooth", "Bluetooth"]);
  for (const p of radio.peers) p.close();
  await mgr.stop();
});

test("waits before it tries a device again after a failed connect", async () => {
  const radio = new FakeRadio();
  radio.fail = true;
  const mgr = new BleManager(radio, { known: () => true, connected: () => false, serve: async () => {} });
  await mgr.start();
  radio.emit({ handle: "p1", id: "aaaaaaaaaaaaaaaa", pairing: false, rssi: -50 });
  await tick();
  radio.emit({ handle: "p1", id: "aaaaaaaaaaaaaaaa", pairing: false, rssi: -50 });
  await tick();
  assert.deepEqual(radio.connects, ["p1"]);
  await mgr.stop();
});

test("skips a device that is already linked over Wi-Fi", async () => {
  const radio = new FakeRadio();
  const mgr = new BleManager(radio, { known: () => true, connected: () => true, serve: async () => {} });
  await mgr.start();
  radio.emit({ handle: "p1", id: "aaaaaaaaaaaaaaaa", pairing: false, rssi: -50 });
  await tick();
  assert.deepEqual(radio.connects, []);
  await mgr.stop();
});


test("an MTU change during a write preserves every byte", async () => {
  let mtu = 20;
  const packets: Buffer[] = [];
  const link: BleLink = {
    get mtu() { return mtu; },
    write: async (p) => { packets.push(Buffer.from(p)); mtu = 244; },
    onData: () => {},
    onClose: () => {},
    close: () => {},
  };
  const pipe = blePipe(link, "test");
  const input = Buffer.from(Array.from({ length: 1000 }, (_, i) => i % 256));
  await new Promise<void>((resolve, reject) => pipe.write(input, (err) => err ? reject(err) : resolve()));
  assert.deepEqual(Buffer.concat(packets), input);
  pipe.destroy();
});

test("a stopped Bluetooth helper rejects a connection", async () => {
  process.env.THRONECTL_BLE_FAKE = "unused";
  try {
    const { loadRadio } = await import("../src/ble/radio.js");
    const loaded = await loadRadio();
    assert.ok("radio" in loaded);
    await loaded.radio.stop();
    await assert.rejects(loaded.radio.connect("gone"), /helper stopped/);
  } finally {
    delete process.env.THRONECTL_BLE_FAKE;
  }
});
