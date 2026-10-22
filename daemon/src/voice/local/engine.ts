import { fork, type ChildProcess } from "node:child_process";
import { createRequire } from "node:module";
import { delimiter, dirname, join } from "node:path";
import type { Socket } from "node:net";
import { fileURLToPath } from "node:url";
import type { Config } from "../../config.js";
import { logger } from "../../log.js";
import { peakAbs } from "../wav.js";
import { localModel, resolveModel, type ResolvedModel } from "./models.js";
import type { SttFiles, TtsFiles, WorkerReply, WorkerRequest } from "./worker.js";

const log = logger("voice-local");

const STT_TIMEOUT_MS = 60_000;
const TTS_TIMEOUT_MS = 120_000; // restarts on each chunk
const IDLE_MS = 5 * 60_000;

/** Kokoro v0.19 speakers, in speaker-id order. */
export const KOKORO_SPEAKERS = [
  "af", "af_bella", "af_nicole", "af_sarah", "af_sky", "am_adam", "am_michael", "bf_emma", "bf_isabella", "bm_george", "bm_lewis",
] as const;

/** Maps a voice setting to a Kokoro speaker id: a speaker name or a number. Empty or unknown gives 0. */
export function kokoroSpeaker(voice: string | undefined): number {
  const v = (voice ?? "").trim().toLowerCase();
  if (!v) return 0;
  if (/^\d+$/.test(v)) return Number(v) < KOKORO_SPEAKERS.length ? Number(v) : 0;
  return Math.max(0, (KOKORO_SPEAKERS as readonly string[]).indexOf(v));
}

/** Paseo's input gain: 0 means silence (peak below 300), else the gain that brings the peak to 0.6, at most 50x. */
export function sttGain(pcm: Buffer): number {
  const peak = peakAbs(pcm);
  if (peak < 300) return 0;
  const p = peak / 32768;
  return p < 0.6 ? Math.min(50, 0.6 / p) : 1;
}

export interface AddonState {
  ok: boolean;
  error?: string;
  version?: string;
}

let addon: AddonState | undefined;

/** Loads sherpa-onnx-node once, if it is installed. THRONECTL_LOCAL_SPEECH=off turns local speech off. */
export function localAddonOk(): AddonState {
  if (process.env.THRONECTL_LOCAL_SPEECH === "off") return { ok: false, error: "turned off by THRONECTL_LOCAL_SPEECH=off" };
  if (!addon) {
    try {
      const s = createRequire(import.meta.url)("sherpa-onnx-node") as { version?: unknown; OfflineTts?: unknown; OfflineRecognizer?: unknown };
      addon = typeof s.OfflineTts === "function" && typeof s.OfflineRecognizer === "function"
        ? { ok: true, ...(typeof s.version === "string" ? { version: s.version } : {}) }
        : { ok: false, error: "sherpa-onnx-node has no OfflineTts or OfflineRecognizer" };
    } catch (e) {
      addon = { ok: false, error: (e instanceof Error ? e.message : String(e)).split("\n")[0] };
    }
  }
  return addon;
}

/** Puts the platform package dir on the library path of the worker, as Paseo does. */
function loaderEnv(): NodeJS.ProcessEnv {
  const env = { ...process.env };
  const name = process.platform === "linux" ? "LD_LIBRARY_PATH" : process.platform === "darwin" ? "DYLD_LIBRARY_PATH" : process.platform === "win32" ? "PATH" : "";
  if (!name) return env;
  try {
    const plat = process.platform === "win32" ? "win" : process.platform;
    const dir = dirname(createRequire(import.meta.url).resolve(`sherpa-onnx-${plat}-${process.arch}/package.json`));
    const key = Object.keys(env).find((k) => k.toLowerCase() === name.toLowerCase()) ?? name;
    const parts = (env[key] ?? "").split(delimiter).filter(Boolean);
    if (!parts.includes(dir)) env[key] = [dir, ...parts].join(delimiter);
  } catch { /* no platform package: the worker reports the load error */ }
  return env;
}

interface Pending {
  w: ChildProcess;
  ms: number;
  timer: NodeJS.Timeout;
  resolve(text: string): void;
  reject(e: Error): void;
  onChunk?: (pcm16k: Buffer) => boolean;
}

let worker: ChildProcess | undefined;
let stderrTail = "";
let idleTimer: NodeJS.Timeout | undefined;
let nextId = 1;
const pending = new Map<number, Pending>();

function spawnWorker(): ChildProcess {
  const w = fork(fileURLToPath(new URL("./worker.js", import.meta.url)), [], {
    env: loaderEnv(), execArgv: [], serialization: "advanced", stdio: ["ignore", "ignore", "pipe", "ipc"],
  });
  stderrTail = "";
  (w.stderr as Socket | null)?.unref();
  w.stderr?.on("data", (d: Buffer) => { stderrTail = (stderrTail + d.toString()).slice(-1000); });
  w.on("message", (m: WorkerReply) => onReply(m));
  w.on("error", (e) => log.warn("voice worker error", e));
  w.on("exit", (code, signal) => {
    if (worker === w) worker = undefined;
    const mine = [...pending].filter(([, p]) => p.w === w);
    if (mine.length) {
      const tail = stderrTail.trim().split("\n").slice(-3).join(" | ");
      const err = new Error(`local speech worker exited (${signal ?? `code ${code}`})${tail ? `: ${tail}` : ""}`);
      for (const [id, p] of mine) settle(id, p)?.reject(err);
      log.warn(err.message);
    } else {
      log.debug(`voice worker exited (${signal ?? `code ${code}`})`);
    }
  });
  log.info(`voice worker started (pid ${w.pid})`);
  return w;
}

/** Keeps the parent alive only while requests run. When idle for 5 minutes the worker is closed. */
function refresh(): void {
  const w = worker;
  if (idleTimer) { clearTimeout(idleTimer); idleTimer = undefined; }
  if (!w) return;
  if ([...pending.values()].some((p) => p.w === w)) {
    w.ref();
    w.channel?.ref();
    return;
  }
  w.unref();
  w.channel?.unref();
  idleTimer = setTimeout(() => { if (worker === w) stopLocalWorker(); }, IDLE_MS);
  idleTimer.unref();
}

function settle(id: number, p: Pending): Pending | undefined {
  if (pending.get(id) !== p) return undefined;
  clearTimeout(p.timer);
  pending.delete(id);
  refresh();
  return p;
}

function onTimeout(id: number): void {
  const p = pending.get(id);
  if (!p) return;
  settle(id, p);
  p.reject(new Error(`local speech timed out after ${p.ms / 1000} s`));
  log.warn("voice worker timed out: restarting it");
  p.w.kill();
}

function stop(id: number, p: Pending): void {
  if (!settle(id, p)) return;
  p.resolve("");
  if (p.w.connected) p.w.send({ type: "stop", id } satisfies WorkerRequest);
}

function onReply(m: WorkerReply): void {
  const p = pending.get(m.id);
  if (!p) return;
  if (m.type === "chunk") {
    p.timer.refresh();
    let more = false;
    try {
      more = p.onChunk?.(Buffer.from(m.pcm.buffer, m.pcm.byteOffset, m.pcm.byteLength)) ?? false;
    } catch (e) {
      settle(m.id, p);
      p.reject(e instanceof Error ? e : new Error(String(e)));
      if (p.w.connected) p.w.send({ type: "stop", id: m.id } satisfies WorkerRequest);
      return;
    }
    if (!more) stop(m.id, p);
    return;
  }
  settle(m.id, p);
  if (m.type === "done") p.resolve(m.text ?? "");
  else p.reject(new Error(m.error));
}

type Job = { type: "stt"; files: SttFiles; threads: number; pcm: Uint8Array; gain: number }
  | { type: "tts"; files: TtsFiles; threads: number; text: string; sid: number; speed: number };

function request(job: Job, ms: number, onChunk?: (pcm16k: Buffer) => boolean): Promise<string> {
  if (!worker || !worker.connected) worker = spawnWorker();
  const w = worker;
  const id = nextId++;
  return new Promise<string>((resolve, reject) => {
    const p: Pending = { w, ms, resolve, reject, onChunk, timer: setTimeout(() => onTimeout(id), ms) };
    pending.set(id, p);
    refresh();
    w.send({ ...job, id } satisfies WorkerRequest, (err) => {
      if (err && settle(id, p)) reject(err);
    });
  });
}

/** Closes the worker, which frees the models. Requests in flight fail. */
export function stopLocalWorker(): void {
  const w = worker;
  worker = undefined;
  if (idleTimer) { clearTimeout(idleTimer); idleTimer = undefined; }
  if (!w) return;
  try { w.disconnect(); } catch { /* already closed */ }
  setTimeout(() => { if (w.exitCode === null && w.signalCode === null) w.kill(); }, 2000).unref();
}

const threads = (cfg: Config): number => Math.max(1, Math.min(16, Math.floor(Number(cfg.voice.local.threads) || 2)));
const speed = (cfg: Config): number => Math.max(0.5, Math.min(2, Number(cfg.voice.local.speed) || 1));

function ready(cfg: Config, id: string, kind: "stt" | "tts"): ResolvedModel {
  const a = localAddonOk();
  if (!a.ok) throw new Error(`local speech is not available: ${a.error}`);
  if (localModel(id)?.kind !== kind) throw new Error(`"${id}" is not a local ${kind.toUpperCase()} model`);
  const r = resolveModel(cfg, id);
  if (!r) throw new Error(`local ${kind.toUpperCase()} model ${id} is not installed. Run: thronectl voice install`);
  return r;
}

/** Transcribes 16 kHz mono PCM16 with Parakeet. Returns "" for silence. */
export async function transcribeLocal(cfg: Config, pcm16k: Buffer): Promise<string> {
  const gain = sttGain(pcm16k);
  if (gain === 0) return "";
  const r = ready(cfg, cfg.voice.local.stt, "stt");
  const [encoder, decoder, joiner, tokens] = r.model.files.map((f) => join(r.path, f));
  const pcm = new Uint8Array(pcm16k.buffer, pcm16k.byteOffset, pcm16k.byteLength);
  return request({ type: "stt", files: { encoder, decoder, joiner, tokens }, threads: threads(cfg), pcm, gain }, STT_TIMEOUT_MS);
}

/** Speaks text with Kokoro. Calls onChunk with 16 kHz PCM16 per sentence. Stops when onChunk returns false. */
export async function synthesizeLocal(cfg: Config, text: string, voice: string, onChunk: (pcm16k: Buffer) => boolean): Promise<void> {
  const clean = text.trim();
  if (!clean) return;
  const r = ready(cfg, cfg.voice.local.tts, "tts");
  const [model, voices, tokens, dataDir] = r.model.files.map((f) => join(r.path, f));
  const sid = kokoroSpeaker(voice || cfg.voice.tts.voice);
  await request({
    type: "tts", files: { model, voices, tokens, dataDir }, threads: threads(cfg), text: clean, sid, speed: speed(cfg),
  }, TTS_TIMEOUT_MS, onChunk);
}
