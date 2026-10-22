/** Child process that runs sherpa-onnx, so native memory and crashes stay out of the daemon. */
import { createRequire } from "node:module";
import { float32ToPcm16, pcm16ToFloat32, resample } from "../wav.js";

export interface SttFiles { encoder: string; decoder: string; joiner: string; tokens: string }
export interface TtsFiles { model: string; voices: string; tokens: string; dataDir: string }

export type WorkerRequest =
  | { type: "stt"; id: number; files: SttFiles; threads: number; pcm: Uint8Array; gain: number }
  | { type: "tts"; id: number; files: TtsFiles; threads: number; text: string; sid: number; speed: number }
  | { type: "stop"; id: number };

export type WorkerReply =
  | { type: "chunk"; id: number; pcm: Uint8Array }
  | { type: "done"; id: number; text?: string }
  | { type: "error"; id: number; error: string };

/** The 16 kHz rate the device speaker and the recognizer use. */
const RATE = 16000;

interface OfflineStream { acceptWaveform(w: { samples: Float32Array; sampleRate: number }): void }
interface Recognizer {
  createStream(): OfflineStream;
  decode(s: OfflineStream): void;
  getResult(s: OfflineStream): { text?: string };
  decodeAsync?(s: OfflineStream): Promise<{ text?: string }>;
}
interface Tts {
  sampleRate: number;
  generateAsync(req: {
    text: string; sid: number; speed: number; enableExternalBuffer: boolean;
    onProgress: (info: { samples: Float32Array; progress: number }) => number;
  }): Promise<unknown>;
}
interface Sherpa {
  OfflineRecognizer: { createAsync?(c: unknown): Promise<Recognizer>; new (c: unknown): Recognizer };
  OfflineTts: { createAsync?(c: unknown): Promise<Tts>; new (c: unknown): Tts };
}

let sherpa: Sherpa | undefined;
function addon(): Sherpa {
  sherpa ??= createRequire(import.meta.url)("sherpa-onnx-node") as Sherpa;
  return sherpa;
}

/** One loaded model per kind. A load that fails is dropped, so the next request tries again. */
class Slot<T> {
  private key = "";
  private p: Promise<T> | undefined;
  get(key: string, load: () => Promise<T>): Promise<T> {
    if (!this.p || this.key !== key) {
      const p = load();
      this.key = key;
      this.p = p;
      p.catch(() => { if (this.p === p) this.p = undefined; });
    }
    return this.p;
  }
}

const recognizer = new Slot<Recognizer>();
const kokoro = new Slot<Tts>();
const stopped = new Set<number>();
let sttQueue: Promise<unknown> = Promise.resolve();
let ttsQueue: Promise<unknown> = Promise.resolve();

function loadRecognizer(f: SttFiles, threads: number): Promise<Recognizer> {
  return recognizer.get(JSON.stringify([f, threads]), async () => {
    const { OfflineRecognizer } = addon();
    const config = {
      featConfig: { sampleRate: RATE, featureDim: 80 },
      modelConfig: {
        transducer: { encoder: f.encoder, decoder: f.decoder, joiner: f.joiner },
        tokens: f.tokens, modelType: "nemo_transducer", numThreads: threads, provider: "cpu", debug: 0,
      },
      decodingMethod: "greedy_search",
      maxActivePaths: 4,
    };
    return OfflineRecognizer.createAsync ? OfflineRecognizer.createAsync(config) : new OfflineRecognizer(config);
  });
}

function loadTts(f: TtsFiles, threads: number): Promise<Tts> {
  return kokoro.get(JSON.stringify([f, threads]), async () => {
    const { OfflineTts } = addon();
    // The native parser reads numThreads and provider under model (same as Paseo).
    const config = {
      model: { numThreads: threads, provider: "cpu", kokoro: { ...f, lengthScale: 1.0 } },
      maxNumSentences: 1,
    };
    return OfflineTts.createAsync ? OfflineTts.createAsync(config) : new OfflineTts(config);
  });
}

function send(msg: WorkerReply): void {
  if (process.connected) process.send?.(msg);
}

const asBuffer = (u: Uint8Array): Buffer => Buffer.from(u.buffer, u.byteOffset, u.byteLength);

async function transcribe(m: Extract<WorkerRequest, { type: "stt" }>): Promise<string> {
  const r = await loadRecognizer(m.files, m.threads);
  const s = r.createStream();
  s.acceptWaveform({ samples: pcm16ToFloat32(asBuffer(m.pcm), m.gain), sampleRate: RATE });
  if (r.decodeAsync) return String((await r.decodeAsync(s)).text ?? "").trim();
  r.decode(s);
  return String(r.getResult(s).text ?? "").trim();
}

/** Sends one chunk per sentence. A stop request ends the generation after the current sentence. */
async function speak(m: Extract<WorkerRequest, { type: "tts" }>): Promise<void> {
  if (stopped.has(m.id)) return;
  const t = await loadTts(m.files, m.threads);
  const rate = t.sampleRate || 24000;
  await t.generateAsync({
    text: m.text, sid: m.sid, speed: m.speed, enableExternalBuffer: false,
    onProgress: ({ samples }) => {
      if (stopped.has(m.id)) return 0;
      const pcm = resample(float32ToPcm16(samples), rate, RATE);
      if (pcm.length) send({ type: "chunk", id: m.id, pcm });
      return 1;
    },
  });
}

/** Runs fn after the work already in the queue, one job at a time. */
function enqueue<T>(queue: "stt" | "tts", fn: () => Promise<T>): Promise<T> {
  const prev = queue === "stt" ? sttQueue : ttsQueue;
  const next = prev.then(fn, fn);
  const settled = next.then(() => undefined, () => undefined);
  if (queue === "stt") sttQueue = settled; else ttsQueue = settled;
  return next;
}

function handle(m: WorkerRequest): void {
  if (m.type === "stop") { stopped.add(m.id); return; }
  const job = m.type === "stt" ? enqueue("stt", () => transcribe(m)) : enqueue("tts", () => speak(m)).then(() => undefined);
  job.then(
    (text) => send({ type: "done", id: m.id, ...(typeof text === "string" ? { text } : {}) }),
    (e: unknown) => send({ type: "error", id: m.id, error: e instanceof Error ? e.message : String(e) }),
  ).finally(() => stopped.delete(m.id));
}

process.title = "thronectl-voice";
process.on("message", (m: WorkerRequest) => handle(m));
// The parent closes the channel when it is idle or exits. Exiting is the only way to free native memory.
process.on("disconnect", () => process.exit(0));
