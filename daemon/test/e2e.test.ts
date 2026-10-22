import { after, before, test } from "node:test";
import assert from "node:assert/strict";
import { mkdtempSync, rmSync } from "node:fs";
import { createServer, connect } from "node:net";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { generateKeyPair } from "../src/crypto.js";
import { Conn } from "../src/session.js";
import { SimDevice } from "../src/sim.js";
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
