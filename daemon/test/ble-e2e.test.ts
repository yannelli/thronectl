import { after, before, test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createServer, type Server, type Socket } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Duplex } from "node:stream";
import { fileURLToPath } from "node:url";
import { generateKeyPair, keyId } from "../src/crypto.js";
import { SimDevice } from "../src/sim.js";
import { encodeWav } from "../src/voice/wav.js";
import type { Daemon as DaemonT } from "../src/server.js";

// The real daemon and Bluetooth helper, with a fake noble whose peripheral is the device below.
const home = mkdtempSync(join(tmpdir(), "thronectl-ble-"));
process.env.THRONECTL_HOME = home;
const kp = generateKeyPair();
const known = new Map<string, string>();
let daemon: DaemonT;
let radio: Server;
let port = 0;
let deviceSide: ((sock: Socket) => void) | undefined;

const listen = (s: Server): Promise<number> => new Promise((r) => s.listen(0, "127.0.0.1", () => r((s.address() as { port: number }).port)));
const until = async <T>(fn: () => T | undefined, ms = 10_000): Promise<T> => {
  const end = Date.now() + ms;
  for (;;) {
    const v = fn();
    if (v) return v;
    if (Date.now() > end) throw new Error("condition not met in time");
    await new Promise((r) => setTimeout(r, 50));
  }
};

before(async () => {
  radio = createServer((s) => deviceSide?.(s));
  const radioPort = await listen(radio);
  const adv = Buffer.alloc(13);
  adv.writeUInt16LE(0xffff, 0);
  adv.write("TC", 2, "latin1");
  Buffer.from(keyId(kp.pub), "hex").copy(adv, 4);
  process.env.THRONECTL_BLE_FAKE = fileURLToPath(new URL("./fixtures/fakenoble.js", import.meta.url));
  process.env.THRONECTL_BLE_FAKE_PORT = String(radioPort);
  process.env.THRONECTL_BLE_FAKE_ADV = adv.toString("hex");
  const { Daemon } = await import("../src/server.js");
  daemon = new Daemon();
  const probe = createServer();
  port = await listen(probe);
  await new Promise((r) => probe.close(r));
  Object.assign(daemon.cfg, { port, bind: "127.0.0.1", discovery: false, projectRoots: [] });
  daemon.cfg.paseo.enabled = false;
  daemon.cfg.ble.enabled = true;
  daemon.broker.on("request", (r: { id: string }) => setTimeout(() => daemon.broker.answer(r.id, true), 20));
});

after(() => {
  daemon?.stop();
  radio.close();
  rmSync(home, { recursive: true, force: true });
  setTimeout(() => process.exit(0), 300).unref();
});

/** Plays the device's GATT side: reads RX slowly, reports credits, and sends TX in 244-byte notifications. */
function emulate(sock: Socket, maxInFlight: { n: number }): Duplex {
  let got = 0, consumed = 0, buf = Buffer.alloc(0);
  const pending: Buffer[] = [];
  const frame = (type: number, b: Buffer): void => {
    const h = Buffer.alloc(3);
    h[0] = type;
    h.writeUInt16LE(b.length, 1);
    sock.write(Buffer.concat([h, b]));
  };
  const app = new Duplex({
    read() {},
    write(chunk: Buffer, _e, done) {
      for (let o = 0; o < chunk.length; o += 244) frame(3, chunk.subarray(o, o + 244));
      done();
    },
    destroy(err, done) { sock.destroy(); done(err); },
  });
  sock.on("data", (d: Buffer) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 3 && buf.length >= 3 + buf.readUInt16LE(1)) {
      const body = buf.subarray(3, 3 + buf.readUInt16LE(1));
      buf = buf.subarray(3 + body.length);
      got += body.length;
      maxInFlight.n = Math.max(maxInFlight.n, got - consumed);
      pending.push(Buffer.from(body));
    }
  });
  const timer = setInterval(() => {
    let n = 0;
    while (pending.length && n < 2048) { const b = pending.shift()!; app.push(b); n += b.length; }
    if (!n) return;
    consumed += n;
    const c = Buffer.alloc(6);
    c.writeUInt32LE(consumed, 0);
    c.writeUInt16LE(247, 4);
    frame(2, c);
  }, 20);
  sock.on("close", () => { clearInterval(timer); app.push(null); app.destroy(); });
  return app;
}

test("a paired device nearby is linked over Bluetooth, with credits pacing the host", async () => {
  await daemon.start();
  const wifi = new SimDevice(kp, "Cardputer-Travel", known);
  await wifi.connect("127.0.0.1", port);
  wifi.close();

  const inFlight = { n: 0 };
  const dev = new SimDevice(kp, "Cardputer-Travel", known);
  const states: string[] = [];
  dev.onAuth = (st) => states.push(st);
  const linked = new Promise<void>((resolve, reject) => {
    deviceSide = (sock) => { dev.attach(emulate(sock, inFlight)).then(() => resolve(), reject); };
  });
  await linked;
  assert.deepEqual(states, ["ok"], "a paired device gets no pairing prompt");

  const wav = join(home, "tone.wav");
  const pcm = Buffer.alloc(16000 * 2 * 2);
  for (let i = 0; i < pcm.length / 2; i++) pcm.writeInt16LE(Math.round(Math.sin(i / 6) * 9000), i * 2);
  writeFileSync(wav, encodeWav(pcm, 16000));
  daemon.cfg.voice.tts = { ...daemon.cfg.voice.tts, provider: "command", command: `cp ${JSON.stringify(wav)} {out}` };
  let audio = 0, ended = false;
  dev.ch!.on("audio", (b: Buffer) => { audio += b.length; });
  dev.onEvent = (e, d) => { if (e === "tts" && (d as { state: string }).state === "end") ended = true; };
  await dev.call("tts.say", { text: "Testing over Bluetooth." });
  await until(() => ended, 15_000);
  assert.equal(audio, pcm.length, "all audio arrives through the credit window");
  assert.ok(inFlight.n <= 12_288, `the host keeps within the window (${inFlight.n} bytes in flight)`);
  const { groups } = await dev.call<{ groups: unknown[] }>("config.groups");
  assert.ok(groups.length > 3);
  dev.close();
  const again = new SimDevice(kp, "Cardputer-Travel", known);
  const reconnected = new Promise<void>((resolve, reject) => {
    deviceSide = (sock) => { again.attach(emulate(sock, { n: 0 })).then(() => resolve(), reject); };
  });
  await reconnected;
  const next = await again.call<{ groups: unknown[] }>("config.groups");
  assert.ok(next.groups.length > 3, "the same peripheral reconnects without duplicate notifications");
  again.close();
});
