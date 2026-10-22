import { after, test } from "node:test";
import assert from "node:assert/strict";
import { mkdirSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { homedir, platform, tmpdir } from "node:os";
import { join } from "node:path";

const tmp = mkdtempSync(join(tmpdir(), "thronectl-voice-"));
process.env.THRONECTL_HOME = join(tmp, "home");
delete process.env.THRONECTL_MODELS_DIR;
delete process.env.THRONECTL_LOCAL_SPEECH;
const realPaseo = join(homedir(), ".paseo", "models", "local-speech");

const { defaultConfig } = await import("../src/config.js");
const { configDir } = await import("../src/paths.js");
const models = await import("../src/voice/local/models.js");
const engine = await import("../src/voice/local/engine.js");
const stt = await import("../src/voice/stt.js");
const tts = await import("../src/voice/tts.js");
const wav = await import("../src/voice/wav.js");
type Config = ReturnType<typeof defaultConfig>;

after(() => {
  engine.stopLocalWorker();
  rmSync(tmp, { recursive: true, force: true });
});

let n = 0;
const freshDir = (): string => { const d = join(tmp, `d${n++}`); mkdirSync(d, { recursive: true }); return d; };

/** Writes a fake model with non-empty files (espeak-ng-data as a directory). */
function fakeModel(root: string, id: string): string {
  const m = models.localModel(id)!;
  const dir = join(root, m.dir);
  mkdirSync(dir, { recursive: true });
  for (const f of m.files) {
    if (f === "espeak-ng-data") mkdirSync(join(dir, f), { recursive: true });
    else writeFileSync(join(dir, f), "x");
  }
  return dir;
}

/** A config with empty model dirs and an OpenAI key, so the fallback order does not depend on the host. */
function testConfig(ours = freshDir(), paseo = freshDir()): Config {
  process.env.PASEO_LOCAL_MODELS_DIR = paseo;
  const cfg = defaultConfig();
  cfg.voice.local.modelsDir = ours;
  cfg.voice.keys.OPENAI_API_KEY = "test";
  return cfg;
}

function tone(seconds: number, amp: number, rate = 16000): Buffer {
  const out = Buffer.alloc(Math.floor(seconds * rate) * 2);
  for (let i = 0; i < out.length / 2; i++) out.writeInt16LE(Math.round(Math.sin(i / 8) * amp * 32767), i * 2);
  return out;
}

test("model catalog has the Paseo models with sound entries", () => {
  const ids = models.LOCAL_MODELS.map((m) => m.id);
  assert.deepEqual(ids, ["parakeet-tdt-0.6b-v2-int8", "parakeet-tdt-0.6b-v3-int8", "kokoro-en-v0_19", "kokoro-int8-en-v0_19"]);
  for (const m of models.LOCAL_MODELS) {
    assert.match(m.url, /^https:\/\/github\.com\/k2-fsa\/sherpa-onnx\/releases\/download\/(asr|tts)-models\//);
    assert.ok(m.url.endsWith(`/${m.dir}.tar.bz2`), m.url);
    assert.match(m.sha256, /^[0-9a-f]{64}$/);
    assert.ok(m.bytes > 100e6 && m.bytes < 600e6, `${m.id} ${m.bytes}`);
    assert.equal(m.files.length, 4);
    assert.equal(m.files.includes("espeak-ng-data"), m.kind === "tts");
  }
  assert.equal(new Set(models.LOCAL_MODELS.map((m) => m.sha256)).size, 4);
  const cfg = defaultConfig();
  assert.equal(models.localModel(cfg.voice.local.stt)?.kind, "stt");
  assert.equal(models.localModel(cfg.voice.local.tts)?.kind, "tts");
  assert.deepEqual(models.localModelIds("tts"), ["kokoro-en-v0_19", "kokoro-int8-en-v0_19"]);
});

test("isInstalled follows Paseo's rule", () => {
  const root = freshDir();
  const id = "parakeet-tdt-0.6b-v2-int8";
  assert.equal(models.isInstalled(root, id), false);
  const dir = fakeModel(root, id);
  assert.equal(models.isInstalled(root, id), true);
  assert.equal(models.isInstalled(root, "no-such-model"), false);
  // A non-empty archive left in .downloads means the install did not finish.
  const archive = models.archivePath(root, models.localModel(id)!);
  mkdirSync(join(root, ".downloads"), { recursive: true });
  writeFileSync(archive, "partial");
  assert.equal(models.isInstalled(root, id), false);
  rmSync(archive);
  writeFileSync(join(dir, "joiner.int8.onnx"), "");
  assert.equal(models.isInstalled(root, id), false, "empty file");
  rmSync(join(dir, "joiner.int8.onnx"));
  assert.equal(models.isInstalled(root, id), false, "missing file");

  const kokoro = fakeModel(root, "kokoro-en-v0_19");
  assert.equal(models.isInstalled(root, "kokoro-en-v0_19"), true, "espeak-ng-data as a directory");
  rmSync(join(kokoro, "espeak-ng-data"), { recursive: true });
  writeFileSync(join(kokoro, "espeak-ng-data"), "");
  assert.equal(models.isInstalled(root, "kokoro-en-v0_19"), false);
});

test("modelsDir and resolveModel order", () => {
  const cfg = defaultConfig();
  process.env.THRONECTL_MODELS_DIR = join(tmp, "env-models");
  assert.equal(models.modelsDir(cfg), join(tmp, "env-models"));
  cfg.voice.local.modelsDir = join(tmp, "cfg-models");
  assert.equal(models.modelsDir(cfg), join(tmp, "cfg-models"));
  delete process.env.THRONECTL_MODELS_DIR;
  assert.equal(models.modelsDir(defaultConfig()), join(configDir(), "models", "local-speech"));

  const ours = freshDir(), paseo = freshDir();
  const c = testConfig(ours, paseo);
  const id = "kokoro-en-v0_19";
  assert.equal(models.resolveModel(c, id), undefined);
  const paseoPath = fakeModel(paseo, id);
  assert.deepEqual([models.resolveModel(c, id)?.source, models.resolveModel(c, id)?.path], ["paseo", paseoPath]);
  const oursPath = fakeModel(ours, id);
  assert.deepEqual([models.resolveModel(c, id)?.source, models.resolveModel(c, id)?.path], ["thronectl", oursPath]);
  mkdirSync(join(ours, ".downloads"));
  writeFileSync(models.archivePath(ours, models.localModel(id)!), "partial");
  assert.equal(models.resolveModel(c, id)?.source, "paseo", "an unfinished install in our dir is skipped");
  assert.equal(models.resolveModel(c, "no-such-model"), undefined);

  const st = models.localVoiceStatus(c);
  assert.equal(st.tts.installed, true);
  assert.equal(st.tts.source, "paseo");
  assert.deepEqual(st.stt, { id: "parakeet-tdt-0.6b-v2-int8", installed: false, source: "" });
  assert.equal(st.installing, false);
});

test("voice names map to Kokoro speakers, and the STT gain follows Paseo", () => {
  assert.equal(engine.kokoroSpeaker(""), 0);
  assert.equal(engine.kokoroSpeaker(undefined), 0);
  assert.equal(engine.kokoroSpeaker("af_sarah"), 3);
  assert.equal(engine.kokoroSpeaker(" AF_Sarah "), 3);
  assert.equal(engine.kokoroSpeaker("bm_lewis"), 10);
  assert.equal(engine.kokoroSpeaker("7"), 7);
  assert.equal(engine.kokoroSpeaker("11"), 0);
  assert.equal(engine.kokoroSpeaker("alloy"), 0);
  assert.equal(engine.KOKORO_SPEAKERS.length, 11);

  assert.equal(engine.sttGain(Buffer.alloc(3200)), 0);
  const quiet = Buffer.alloc(4); quiet.writeInt16LE(299, 0);
  assert.equal(engine.sttGain(quiet), 0);
  const p = Buffer.alloc(4); p.writeInt16LE(-3277, 2);
  assert.ok(Math.abs(engine.sttGain(p) - 0.6 / (3277 / 32768)) < 1e-9);
  p.writeInt16LE(300, 2);
  assert.equal(engine.sttGain(p), 50);
  p.writeInt16LE(30000, 2);
  assert.equal(engine.sttGain(p), 1);

  const src = tone(0.01, 0.5);
  const back = wav.float32ToPcm16(wav.pcm16ToFloat32(src, 1));
  assert.equal(back.length, src.length);
  for (let i = 0; i < src.length; i += 2) assert.ok(Math.abs(back.readInt16LE(i) - src.readInt16LE(i)) <= 1, "round trip within 1 LSB");
  assert.equal(wav.pcm16ToFloat32(p, 2)[1], 1, "gain clips at 1");
  assert.equal(wav.float32ToPcm16(new Float32Array([2, -2])).readInt16LE(2), -32767);
});

test("auto falls back when the models or the addon are missing", () => {
  const cfg = testConfig();
  assert.equal(stt.pickStt(cfg), "openai");
  if (platform() !== "darwin") assert.equal(tts.pickTts(cfg), "openai");
  fakeModel(cfg.voice.local.modelsDir!, cfg.voice.local.stt);
  fakeModel(cfg.voice.local.modelsDir!, cfg.voice.local.tts);
  if (engine.localAddonOk().ok) {
    assert.equal(stt.pickStt(cfg), "local");
    assert.equal(tts.pickTts(cfg), "local");
    assert.match(stt.describeStt(cfg), /^local \(parakeet-tdt-0\.6b-v2-int8\)$/);
    assert.match(tts.describeTts(cfg), /^local \(kokoro-en-v0_19\)$/);
  }
  process.env.THRONECTL_LOCAL_SPEECH = "off";
  try {
    assert.equal(engine.localAddonOk().ok, false);
    assert.equal(stt.pickStt(cfg), "openai");
    if (platform() !== "darwin") assert.equal(tts.pickTts(cfg), "openai");
    assert.equal(models.localVoiceStatus(cfg).addon, false);
  } finally {
    delete process.env.THRONECTL_LOCAL_SPEECH;
  }
  cfg.voice.local.stt = "kokoro-en-v0_19";
  assert.notEqual(stt.pickStt(cfg), "local", "a TTS model is not an STT model");
});

test("an explicit local provider with no model fails with a clear error", async () => {
  const cfg = testConfig();
  cfg.voice.stt.provider = "local";
  cfg.voice.tts.provider = "local";
  assert.equal(stt.pickStt(cfg), "local");
  assert.match(stt.describeStt(cfg), /not installed: run thronectl voice install/);
  assert.match(tts.describeTts(cfg), /not installed: run thronectl voice install/);
  await assert.rejects(stt.transcribe(cfg, tone(0.5, 0.3), 16000), /not installed|not available/);
  await assert.rejects(tts.synthesize(cfg, "hello"), /not installed|not available/);
  assert.equal(await engine.transcribeLocal(cfg, Buffer.alloc(32000)), "", "silence needs no model");
  await assert.rejects(models.installLocalVoice({ ...cfg, voice: { ...cfg.voice, local: { ...cfg.voice.local, stt: "x" } } }), /unknown STT model "x"/);
});

test("synthesizeStream passes the whole buffer once for other providers", async () => {
  const cfg = testConfig();
  const pcm = tone(0.1, 0.2);
  const b64 = wav.encodeWav(pcm, 16000).toString("base64");
  cfg.voice.tts.provider = "command";
  cfg.voice.tts.command = `${JSON.stringify(process.execPath)} -e "process.stdout.write(Buffer.from('${b64}','base64'))"`;
  const chunks: Buffer[] = [];
  await tts.synthesizeStream(cfg, "hello", undefined, (c) => { chunks.push(c); return true; });
  assert.equal(chunks.length, 1);
  assert.deepEqual(chunks[0], pcm);
});

const realReady = engine.localAddonOk().ok
  && models.isInstalled(realPaseo, "parakeet-tdt-0.6b-v2-int8") && models.isInstalled(realPaseo, "kokoro-en-v0_19");

test("real models: Kokoro streams sentences and Parakeet reads them back", { skip: realReady ? false : "needs sherpa-onnx-node and Paseo's models in ~/.paseo/models/local-speech", timeout: 180_000 }, async () => {
  const cfg = testConfig(freshDir(), realPaseo);
  cfg.voice.stt.provider = "local";
  cfg.voice.tts.provider = "local";
  const text = "Build finished. All tests passed. Ready for review.";
  for (const round of ["first call", "warm"]) {
    const chunks: Buffer[] = [];
    const t0 = Date.now();
    let firstMs = 0;
    await tts.synthesizeStream(cfg, text, "af_sarah", (c) => { firstMs ||= Date.now() - t0; chunks.push(c); return true; });
    const ttsMs = Date.now() - t0;
    const pcm = Buffer.concat(chunks);
    assert.ok(chunks.length > 1, `chunks: ${chunks.length}`);
    assert.ok(chunks.every((c) => c.length > 0 && c.length % 2 === 0));
    const seconds = pcm.length / 2 / 16000;
    assert.ok(seconds > 1.5 && seconds < 10, `audio ${seconds} s at 16 kHz`);
    const t1 = Date.now();
    const heard = await stt.transcribe(cfg, pcm, 16000);
    const sttMs = Date.now() - t1;
    const words = heard.toLowerCase().replace(/[^a-z ]/g, " ");
    for (const w of ["build", "finished", "tests", "passed", "review"]) assert.ok(words.includes(w), `"${heard}" lacks "${w}"`);
    console.log(`# ${round}: TTS first chunk ${firstMs} ms, all ${chunks.length} chunks ${ttsMs} ms (${seconds.toFixed(2)} s audio); STT ${sttMs} ms: ${heard}`);
  }
  let got = 0;
  await engine.synthesizeLocal(cfg, "One. Two. Three. Four. Five. Six.", "bm_george", () => { got++; return false; });
  assert.equal(got, 1, "stops after the first chunk");
});
