import { test } from "node:test";
import assert from "node:assert/strict";
import { appImage, compareVersions, OtaStore, versionOf, type Firmware } from "../src/ota.js";

function fakeApp(len: number): Buffer {
  const b = Buffer.alloc(len, 0x5a);
  b[0] = 0xe9;
  return b;
}

/** Bootloader at 0x0, partition table at 0x8000 (nvs, then the app at 0x10000), app at 0x10000. */
function fakeMerged(app: Buffer): Buffer {
  const b = Buffer.alloc(0x10000 + app.length, 0xff);
  b[0] = 0xe9;
  const entry = (off: number, type: number, subtype: number, start: number, size: number): void => {
    b[off] = 0xaa; b[off + 1] = 0x50; b[off + 2] = type; b[off + 3] = subtype;
    b.writeUInt32LE(start, off + 4);
    b.writeUInt32LE(size, off + 8);
  };
  entry(0x8000, 1, 2, 0x9000, 0x5000);
  entry(0x8020, 0, 0x10, 0x10000, 0x330000);
  app.copy(b, 0x10000);
  return b;
}

test("appImage cuts a merged image at its first app partition", () => {
  const app = fakeApp(5000);
  assert.deepEqual(appImage(fakeMerged(app)), app);
});

test("appImage passes an app image through and rejects other files", () => {
  const app = fakeApp(0x9000);
  assert.equal(appImage(app), app);
  assert.throws(() => appImage(Buffer.from("hello world, not firmware".repeat(20))), /not an ESP32/);
});

test("versions come from file names and compare numerically", () => {
  assert.equal(versionOf("/x/thronectl-cardputer-adv-v0.10.2.bin"), "0.10.2");
  assert.equal(versionOf("firmware.bin"), "");
  assert.ok(compareVersions("0.10.0", "0.9.9") > 0);
  assert.ok(compareVersions("0.2.0", "0.2.0") === 0);
  assert.ok(compareVersions("0.2.0", "") > 0);
});

test("OtaStore serves chunks, tracks progress and records the result", () => {
  const app = fakeApp(20000);
  const fw: Firmware = { path: "x", version: "1.2.3", app, sha256: "", source: "test" };
  const store = new OtaStore();
  assert.throws(() => store.read("dev", 0, 100), /no firmware/);
  store.offer("dev", "Cardputer", fw);
  assert.equal(store.read("dev", 0, 8192).length, 8192);
  assert.equal(store.read("dev", 16384, 8192).length, 20000 - 16384);
  assert.throws(() => store.read("dev", 30000, 10), /bad offset/);
  assert.equal(store.list()[0].state, "sending");
  assert.equal(store.list()[0].sent, 20000);
  store.finish("dev", true, "", "sd");
  assert.deepEqual({ state: store.list()[0].state, dest: store.list()[0].dest }, { state: "done", dest: "sd" });
  store.offer("dev", "Cardputer", fw);
  store.disconnected("dev");
  assert.equal(store.list()[0].err, "device disconnected");
});
