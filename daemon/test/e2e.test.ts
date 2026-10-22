import { after, before, test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { createServer, connect } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { generateKeyPair } from "../src/crypto.js";
import { Conn } from "../src/session.js";
import { SimDevice } from "../src/sim.js";
import { encodeWav } from "../src/voice/wav.js";
import { blePipe, fakeRadio } from "../src/ble/pipe.js";
import type { Daemon as DaemonT } from "../src/server.js";

const home = mkdtempSync(join(tmpdir(), "thronectl-test-"));
process.env.THRONECTL_HOME = home;
let daemon: DaemonT;
let port = 0;
let approve = true;

function freePort(): Promise<number> {
  return new Promise((resolve) => {
    const s = createServer().listen(0, "127.0.0.1", () => {
      const p = (s.address() as { port: number }).port;
      s.close(() => resolve(p));
    });
  });
}

const sleep = (ms: number): Promise<void> => new Promise((r) => setTimeout(r, ms));

async function until<T>(fn: () => Promise<T | undefined> | T | undefined, ms = 8000): Promise<T> {
  const end = Date.now() + ms;
  for (;;) {
    const v = await fn();
    if (v) return v;
    if (Date.now() > end) throw new Error("condition not met in time");
    await sleep(100);
  }
}

before(async () => {
  const { Daemon } = await import("../src/server.js");
  daemon = new Daemon();
  port = await freePort();
  daemon.cfg.port = port;
  daemon.cfg.bind = "127.0.0.1";
  daemon.cfg.discovery = false;
  daemon.cfg.paseo.enabled = false;
  daemon.cfg.ble.enabled = false;
  daemon.cfg.projectRoots = [];
  daemon.cfg.shell = "/bin/sh";
  daemon.cfg.harness = {
    echo: { bin: "/bin/sh", name: "Echo", args: ["-c", "echo READY $0; while read l; do echo got $l; done"] },
    ask: {
      bin: "/bin/sh", name: "Ask",
      args: ["-c", "seq 1 30; echo 'Do you want to run: rm -rf build? [y/n]'; read l; echo \"got $l\"; head -c 60000 /dev/zero | tr '\\0' x; echo; while read l; do echo got $l; done"],
    },
  };
  daemon.broker.on("request", (r: { id: string }) => setTimeout(() => daemon.broker.answer(r.id, approve), 50));
  await daemon.start();
});

after(() => {
  daemon.stop();
  rmSync(home, { recursive: true, force: true });
  setTimeout(() => process.exit(0), 200).unref();
});

test("denied pairing closes the connection", async () => {
  approve = false;
  const dev = new SimDevice(generateKeyPair(), "Denied", new Map());
  const codes: string[] = [];
  dev.onAuth = (state, m) => { if (state === "pair") codes.push(String(m.code)); };
  await assert.rejects(dev.connect("127.0.0.1", port), /denied|closed/);
  assert.match(codes[0], /^\d{6}$/);
  approve = true;
});

test("bad commitment is rejected", async () => {
  const sock = connect(port, "127.0.0.1");
  await new Promise((r) => sock.once("connect", r));
  const conn = new Conn(sock);
  const kp = generateKeyPair();
  conn.sendJson({ t: "hello", v: 1, name: "x", fw: "0", pk: kp.pub.toString("base64"), epk: kp.pub.toString("base64"), c: Buffer.alloc(32).toString("base64") });
  await conn.next(3000);
  conn.sendJson({ t: "reveal", n: Buffer.alloc(16, 1).toString("base64"), known: false });
  await until(() => conn.closed || undefined, 3000);
});

test("pair, reconnect without prompt, and use the API", async () => {
  const keys = generateKeyPair();
  const known = new Map<string, string>();
  const first = new SimDevice(keys, "Cardputer-Test", known);
  let paired = false;
  first.onAuth = (s) => { if (s === "pair") paired = true; };
  const host = await first.connect("127.0.0.1", port);
  assert.ok(paired, "first connection asks to pair");
  assert.equal(host.id, daemon.identity.id);
  first.close();
  await sleep(100);

  const dev = new SimDevice(keys, "Cardputer-Test", known);
  let pairedAgain = false;
  dev.onAuth = (s) => { if (s === "pair") pairedAgain = true; };
  const events: { e: string; d: Record<string, unknown> }[] = [];
  dev.onEvent = (e, d) => events.push({ e, d: d as Record<string, unknown> });
  await dev.connect("127.0.0.1", port);
  assert.equal(pairedAgain, false, "trusted device skips pairing");

  const ping = await dev.call<{ t: number }>("ping");
  assert.ok(ping.t > 0);
  await dev.call("sub", { topics: ["stats", "agents", "notify"] });
  const stats = await until(() => events.find((x) => x.e === "stats")?.d);
  assert.equal(typeof stats.cpu, "number");
  assert.equal((stats.mem as number[]).length, 2);

  const { harness } = await dev.call<{ harness: { id: string; ok: boolean }[] }>("harness.list");
  assert.ok(harness.some((h) => h.id === "direct:echo" && h.ok));
  const { projects } = await dev.call<{ projects: { path: string }[] }>("projects.list");
  assert.ok(projects.some((p) => p.path === "~"));
  const { procs } = await dev.call<{ procs: { pid: number }[] }>("procs.list", { n: 5 });
  assert.ok(procs.length > 0);

  const { id } = await dev.call<{ id: string }>("agent.create", { harness: "direct:echo", cwd: "~", prompt: "hello" });
  await until(async () => (await dev.call<{ lines: string[] }>("agent.tail", { id, n: 10 })).lines.join("\n").includes("READY hello") || undefined);
  await dev.call("agent.send", { id, text: "ping" });
  await until(async () => (await dev.call<{ lines: string[] }>("agent.tail", { id, n: 10 })).lines.join("\n").includes("got ping") || undefined);
  await until(() => events.find((x) => x.e === "agents" && (x.d.agents as { id: string }[]).some((a) => a.id === id)));
  const done = await until(() => events.find((x) => x.e === "notify" && x.d.agent === id && x.d.kind === "done"), 10_000);
  assert.equal(typeof done.d.secs, "number", "done alerts carry the turn length");

  const { tid } = await dev.call<{ tid: number }>("term.open", { agent: id, cols: 40, rows: 10 });
  await until(() => events.find((x) => x.e === "term" && x.d.tid === tid && x.d.full));
  await dev.call("term.close", { tid });
  await dev.call("agent.kill", { id });
  const after = await dev.call<{ agents: { id: string }[] }>("agents.list");
  assert.ok(!after.agents.some((a) => a.id === id));

  const shell = await dev.call<{ tid: number }>("term.open", { shell: true, cols: 40, rows: 8 });
  await dev.call("term.input", { tid: shell.tid, d: "echo th$((1+1))rone\r" });
  await until(() => events.some((x) => x.e === "term" && x.d.tid === shell.tid && JSON.stringify(x.d.rows).includes("th2rone")) || undefined);
  await dev.call("term.input", { tid: shell.tid, d: "exit\r" });
  await until(() => events.find((x) => x.e === "term_exit" && x.d.tid === shell.tid));

  await assert.rejects(dev.call("agent.send", { id: "d-999", text: "x" }), /no such agent/);
  await assert.rejects(dev.call("nope"), /unknown method/);
  dev.close();
});

test("each device gets stats at the interval it asked for", async () => {
  const keys = generateKeyPair();
  const known = new Map<string, string>();
  const fast = new SimDevice(keys, "Fast", known);
  await fast.connect("127.0.0.1", port);
  const slow = new SimDevice(generateKeyPair(), "Slow", new Map());
  await slow.connect("127.0.0.1", port);
  const counts = { fast: 0, slow: 0, off: 0 };
  fast.onEvent = (e) => { if (e === "stats") counts.fast++; };
  slow.onEvent = (e) => { if (e === "stats") counts.slow++; };
  await fast.call("sub", { topics: ["stats"], ms: 1000 });
  await slow.call("sub", { topics: ["stats"], ms: 60_000 });
  await sleep(3300);
  assert.ok(counts.fast >= 3, `fast device got ${counts.fast} stats`);
  assert.equal(counts.slow, 1, "slow device gets the first sample only");
  await fast.call("sub", { topics: ["agents"] });
  fast.onEvent = (e) => { if (e === "stats") counts.off++; };
  await sleep(1600);
  assert.equal(counts.off, 0, "no stats topic means no stats");
  fast.close();
  slow.close();
});

test("approvals: pending ask, deny with a comment, and output capped for the device", async () => {
  const dev = new SimDevice(generateKeyPair(), "Cardputer-Ask", new Map());
  await dev.connect("127.0.0.1", port);
  const { id } = await dev.call<{ id: string }>("agent.create", { harness: "direct:ask", cwd: "~", prompt: "" });
  await until(async () => (await dev.call<{ agents: { id: string; status: string }[] }>("agents.list")).agents.find((a) => a.id === id && a.status === "waiting"), 12_000);
  const ask = await dev.call<{ tool: string; text: string }>("agent.pending", { id });
  assert.equal(ask.tool, "Ask");
  assert.match(ask.text, /Do you want to run: rm -rf build/);
  await dev.call("agent.permit", { id, allow: false, message: "use make clean" });
  await until(async () => (await dev.call<{ lines: string[] }>("agent.tail", { id, n: 200 })).lines.join("\n").includes("use make clean") || undefined);
  const { lines } = await until(async () => {
    const r = await dev.call<{ lines: string[] }>("agent.tail", { id, n: 200 });
    return r.lines.join("").includes("xxxx") ? r : undefined;
  });
  assert.ok(Buffer.byteLength(JSON.stringify(lines)) <= 12_000 + 1024, "tail fits the device budget");
  assert.ok(lines.every((l) => l.length <= 400));
  await dev.call("agent.kill", { id });
  dev.close();
});

test("device asks the host for an update and gets an offer it does not need to confirm", async () => {
  process.env.THRONECTL_NO_RELEASES = "1";  // local builds only, no GitHub
  const dev = new SimDevice(generateKeyPair(), "Cardputer-Update", new Map());
  const offers: Record<string, unknown>[] = [];
  dev.onEvent = (e, d) => { if (e === "ota.offer") offers.push(d as Record<string, unknown>); };
  await dev.connect("127.0.0.1", port);
  const info = await dev.call<{ fw: string; latest: string; newer: boolean; host: string; hostUpdate: boolean }>("update.check", { force: true });
  assert.ok(info.host && info.fw, "reports both versions");
  if (!info.latest) {
    await assert.rejects(dev.call("update.firmware"), /no firmware found/);
  } else {
    const r = await dev.call<{ version: string }>("update.firmware");
    assert.equal(r.version, info.latest);
    const offer = await until(() => offers[0]);
    assert.equal(offer.asked, true);
    assert.equal(offer.ver, info.latest);
  }
  delete process.env.THRONECTL_NO_RELEASES;
  dev.close();
});

test("device reads and changes daemon settings, keys and harnesses", async () => {
  const dev = new SimDevice(generateKeyPair(), "Cardputer-Config", new Map());
  await dev.connect("127.0.0.1", port);
  type F = { key: string; type: string; value: unknown; restart?: boolean };
  const { groups } = await dev.call<{ groups: { id: string }[] }>("config.groups");
  assert.ok(groups.some((g) => g.id === "keys"));

  assert.deepEqual(await dev.call("config.set", { key: "name", value: "Laptop" }), { restart: false });
  assert.equal(daemon.cfg.name, "Laptop");
  assert.deepEqual(await dev.call("config.set", { key: "discovery", value: true }), { restart: true });
  daemon.cfg.discovery = false;
  await dev.call("config.set", { key: "projectRoots", value: "/tmp/a\n/tmp/b" });
  assert.deepEqual(daemon.cfg.projectRoots, ["/tmp/a", "/tmp/b"]);
  daemon.cfg.projectRoots = [];
  await assert.rejects(dev.call("config.set", { key: "port", value: "x" }), /must be a number/);
  await assert.rejects(dev.call("config.set", { key: "voice.stt.apiKey", value: "x" }), /cannot be set/);

  await dev.call("config.set", { key: "env.THRONE_TEST_KEY", value: "sk-abcdefghijklmnop" });
  assert.equal(process.env.THRONE_TEST_KEY, "sk-abcdefghijklmnop");
  let keys = (await dev.call<{ fields: F[] }>("config.fields", { group: "keys" })).fields;
  assert.equal(keys.find((f) => f.key === "env.THRONE_TEST_KEY")?.value, "sk-...mnop", "secrets come back masked");
  await dev.call("config.set", { key: "env.THRONE_TEST_KEY", value: null });
  keys = (await dev.call<{ fields: F[] }>("config.fields", { group: "keys" })).fields;
  assert.ok(!keys.some((f) => f.key === "env.THRONE_TEST_KEY"));
  assert.equal(process.env.THRONE_TEST_KEY, undefined);

  await dev.call("config.add", { kind: "harness", id: "mytool", value: "/bin/cat" });
  const links = (await dev.call<{ fields: F[] }>("config.fields", { group: "harnesses" })).fields;
  assert.equal(links.find((f) => f.key === "harness:mytool")?.value, "ready");
  await dev.call("config.set", { key: "harness.mytool.args", value: "-u" });
  assert.deepEqual(daemon.cfg.harness.mytool.args, ["-u"]);
  await dev.call("config.action", { key: "remove:mytool" });
  assert.equal(daemon.cfg.harness.mytool, undefined);
  await assert.rejects(dev.call("config.action", { key: "remove:claude" }), /only be disabled/);
  await dev.call("config.set", { key: "voice.localOn", value: false });
  assert.equal(daemon.cfg.voice.local.enabled, false);
  let voice = (await dev.call<{ fields: F[] }>("config.fields", { group: "voice" })).fields;
  assert.equal(voice.find((f) => f.key === "voice.localOn")?.value, false);
  assert.ok(voice.some((f) => f.key === "voice.install" && f.type === "action"));
  await dev.call("config.set", { key: "voice.localOn", value: true });
  voice = (await dev.call<{ fields: F[] }>("config.fields", { group: "voice" })).fields;
  assert.equal(voice.find((f) => f.key === "voice.localOn")?.value, true);
  await dev.call("config.set", { key: "allowShell", value: false });
  await assert.rejects(dev.call("config.set", { key: "allowShell", value: true }), /read-only/);
  await assert.rejects(dev.call("config.add", { kind: "harness", id: "sh2", value: "/bin/sh" }), /read-only/);
  daemon.cfg.allowShell = true;
  dev.close();
});

test("speech streams to the device at playback pace and the reply comes before the end", async () => {
  const wav = join(home, "tone.wav");
  const pcm = Buffer.alloc(16000 * 2 * 3);  // 3 s at 16 kHz
  for (let i = 0; i < pcm.length / 2; i++) pcm.writeInt16LE(Math.round(Math.sin(i / 8) * 8000), i * 2);
  writeFileSync(wav, encodeWav(pcm, 16000));
  const saved = structuredClone(daemon.cfg.voice.tts);
  daemon.cfg.voice.tts = { ...saved, provider: "command", command: `cp ${JSON.stringify(wav)} {out}` };
  const dev = new SimDevice(generateKeyPair(), "Cardputer-Speak", new Map());
  const events: string[] = [];
  dev.onEvent = (e, d) => { if (e === "tts") events.push((d as { state: string }).state); };
  await dev.connect("127.0.0.1", port);
  let bytes = 0, firstAt = 0;
  dev.ch!.on("audio", (b: Buffer) => { bytes += b.length; firstAt ||= Date.now(); });
  const t0 = Date.now();
  const r = await dev.call<{ ms: number }>("tts.say", { text: "Testing the speaker now." });
  assert.ok(r.ms > 0);
  await until(() => events.includes("end"), 8000);
  const took = Date.now() - t0;
  assert.equal(bytes, pcm.length, "every sample arrives");
  assert.ok(took >= 2400, `paced to playback, not sent at once (${took} ms)`);
  assert.deepEqual(events, ["start", "end"]);
  daemon.cfg.voice.tts = saved;
  dev.close();
});

test("a device paired over Wi-Fi connects over a Bluetooth pipe without a new prompt", async () => {
  const kp = generateKeyPair();
  const known = new Map<string, string>();
  const wifi = new SimDevice(kp, "Cardputer-BLE", known);
  await wifi.connect("127.0.0.1", port);
  wifi.close();
  const [hostEnd, devEnd] = fakeRadio(20);
  const served = daemon.serve(blePipe(hostEnd, "Bluetooth"), "Bluetooth");
  const dev = new SimDevice(kp, "Cardputer-BLE", known);
  const states: string[] = [];
  dev.onAuth = (st) => states.push(st);
  await dev.attach(blePipe(devEnd, "host"));
  await served;
  assert.deepEqual(states, ["ok"], "no pairing prompt for a known device");
  const { groups } = await dev.call<{ groups: unknown[] }>("config.groups");
  assert.ok(groups.length > 3, "a reply of many packets arrives whole");
  const name = daemon.cfg.name;
  await dev.call("config.set", { key: "name", value: "N".repeat(2000) });
  assert.equal(daemon.cfg.name.length, 2000, "a request of many packets arrives whole");
  daemon.cfg.name = name;
  dev.close();
});
