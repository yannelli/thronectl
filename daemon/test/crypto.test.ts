import { test } from "node:test";
import assert from "node:assert/strict";
import { deriveKeys, generateKeyPair, keyId, open, random, seal, x25519 } from "../src/crypto.js";
import { encodeFrame, FrameReader } from "../src/framing.js";
import { decodeWav, encodeWav, resample } from "../src/voice/wav.js";
import { speakable } from "../src/voice/tts.js";
import { parseAge } from "../src/agents/paseo.js";

test("both sides derive the same keys and code", () => {
  const d = generateKeyPair(), de = generateKeyPair(), h = generateKeyPair(), he = generateKeyPair();
  const dn = random(16), hn = random(16);
  const dev = deriveKeys({ dpk: d.pub, depk: de.pub, dn, hpk: h.pub, hepk: he.pub, hn, ss: x25519(d, h.pub), ee: x25519(de, he.pub) });
  const host = deriveKeys({ dpk: d.pub, depk: de.pub, dn, hpk: h.pub, hepk: he.pub, hn, ss: x25519(h, d.pub), ee: x25519(he, de.pub) });
  assert.deepEqual(dev.dh, host.dh);
  assert.deepEqual(dev.hd, host.hd);
  assert.equal(dev.sas, host.sas);
  assert.match(dev.sas, /^\d{6}$/);
  assert.notDeepEqual(dev.dh, dev.hd);
});

test("RFC 7748 X25519 test vector", () => {
  const alice = { priv: Buffer.from("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", "hex"), pub: Buffer.from("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", "hex") };
  const bobPub = Buffer.from("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", "hex");
  assert.equal(x25519(alice, bobPub).toString("hex"), "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
});

test("seal/open round trip and tamper detection", () => {
  const key = random(32);
  const msg = Buffer.from("hello cardputer");
  const sealed = seal(key, 7n, msg);
  assert.deepEqual(open(key, 7n, sealed), msg);
  assert.throws(() => open(key, 8n, sealed));
  sealed[0] ^= 1;
  assert.throws(() => open(key, 7n, sealed));
});

test("key id is 16 hex chars", () => {
  assert.match(keyId(generateKeyPair().pub), /^[0-9a-f]{16}$/);
});

test("frame reader handles split and merged chunks", () => {
  const got: string[] = [];
  const r = new FrameReader((f) => got.push(f.toString()), (e) => assert.fail(e));
  const all = Buffer.concat([encodeFrame(Buffer.from("one")), encodeFrame(Buffer.from("two")), encodeFrame(Buffer.from(""))]);
  for (let i = 0; i < all.length; i += 3) r.push(all.subarray(i, i + 3));
  assert.deepEqual(got, ["one", "two", ""]);
});

test("frame reader rejects oversized frames", () => {
  let err: Error | undefined;
  const r = new FrameReader(() => assert.fail("no frame"), (e) => { err = e; });
  const head = Buffer.alloc(4);
  head.writeUInt32BE(70000);
  r.push(head);
  assert.ok(err);
});

test("wav encode/decode and resample", () => {
  const pcm = Buffer.alloc(3200);
  for (let i = 0; i < 1600; i++) pcm.writeInt16LE(Math.round(Math.sin(i / 10) * 10000), i * 2);
  const back = decodeWav(encodeWav(pcm, 16000));
  assert.equal(back.rate, 16000);
  assert.deepEqual(back.pcm, pcm);
  assert.equal(resample(pcm, 16000, 8000).length, 1600);
});

test("speakable strips markdown and code", () => {
  const s = speakable("## Done\n```ts\nconst x = 1;\n```\nI fixed `login.ts`. See [docs](http://x).", 200);
  assert.equal(s, "Done (code omitted) I fixed login.ts. See docs.");
});

test("paseo relative ages", () => {
  assert.equal(parseAge("3 minutes ago"), 180);
  assert.equal(parseAge("an hour ago"), 3600);
  assert.equal(parseAge("11 days ago"), 11 * 86400);
  assert.equal(parseAge("just now"), 0);
});
