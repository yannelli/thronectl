import { spawn } from "node:child_process";
import { createHash } from "node:crypto";
import { createReadStream, createWriteStream, statSync } from "node:fs";
import { mkdir, rename, rm } from "node:fs/promises";
import { homedir, platform } from "node:os";
import { basename, join } from "node:path";
import { Readable, Transform } from "node:stream";
import { pipeline } from "node:stream/promises";
import type { ReadableStream as WebReadableStream } from "node:stream/web";
import type { Config } from "../../config.js";
import { which } from "../../env.js";
import { configDir, expandHome } from "../../paths.js";
import { localAddonOk } from "./engine.js";

export type LocalModelKind = "stt" | "tts";

/** One sherpa-onnx model archive. STT files: encoder, decoder, joiner, tokens. TTS files: model, voices, tokens, espeak-ng-data. */
export interface LocalModel {
  id: string;
  kind: LocalModelKind;
  label: string;
  url: string;
  dir: string; // directory inside the archive and under the models dir
  files: string[];
  bytes: number; // archive size
  sha256: string;
}

const REL = "https://github.com/k2-fsa/sherpa-onnx/releases/download";
const PARAKEET_FILES = ["encoder.int8.onnx", "decoder.int8.onnx", "joiner.int8.onnx", "tokens.txt"];

/** The models Paseo uses, plus the smaller int8 Kokoro. Sizes and sha256 are the GitHub release assets. */
export const LOCAL_MODELS: readonly LocalModel[] = [
  {
    id: "parakeet-tdt-0.6b-v2-int8", kind: "stt", label: "Parakeet TDT 0.6B v2 (English)",
    url: `${REL}/asr-models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8.tar.bz2`,
    dir: "sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8", files: PARAKEET_FILES, bytes: 482_468_385,
    sha256: "157c157bc51155e03e37d2466522a3a737dd9c72bb25f36eb18912964161e1ad",
  },
  {
    id: "parakeet-tdt-0.6b-v3-int8", kind: "stt", label: "Parakeet TDT 0.6B v3 (25 European languages)",
    url: `${REL}/asr-models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2`,
    dir: "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8", files: PARAKEET_FILES, bytes: 487_170_055,
    sha256: "5793d0fd397c5778d2cf2126994d58e9d56b1be7c04d13c7a15bb1b4eafb16bf",
  },
  {
    id: "kokoro-en-v0_19", kind: "tts", label: "Kokoro 82M v0.19 (English, 11 voices)",
    url: `${REL}/tts-models/kokoro-en-v0_19.tar.bz2`,
    dir: "kokoro-en-v0_19", files: ["model.onnx", "voices.bin", "tokens.txt", "espeak-ng-data"], bytes: 319_625_534,
    sha256: "912804855a04745fa77a30be545b3f9a5d15c4d66db00b88cbcd4921df605ac7",
  },
  {
    id: "kokoro-int8-en-v0_19", kind: "tts", label: "Kokoro 82M v0.19 int8 (English, 11 voices, smaller)",
    url: `${REL}/tts-models/kokoro-int8-en-v0_19.tar.bz2`,
    dir: "kokoro-int8-en-v0_19", files: ["model.int8.onnx", "voices.bin", "tokens.txt", "espeak-ng-data"], bytes: 103_248_205,
    sha256: "c9f0dd393615805b0bab050c340834d5e684e732aec91c0e860cd30e982c08bd",
  },
];

export function localModel(id: string): LocalModel | undefined {
  return LOCAL_MODELS.find((m) => m.id === id);
}

/** Model ids of one kind, for help and error text. */
export function localModelIds(kind: LocalModelKind): string[] {
  return LOCAL_MODELS.filter((m) => m.kind === kind).map((m) => m.id);
}

/** Our models dir: voice.local.modelsDir, else THRONECTL_MODELS_DIR, else ~/.config/thronectl/models/local-speech. */
export function modelsDir(cfg: Config): string {
  const dir = cfg.voice.local.modelsDir || process.env.THRONECTL_MODELS_DIR;
  return dir ? expandHome(dir) : join(configDir(), "models", "local-speech");
}

/** Paseo's models dir, which we only read. */
export function paseoModelsDir(): string {
  if (process.env.PASEO_LOCAL_MODELS_DIR) return expandHome(process.env.PASEO_LOCAL_MODELS_DIR);
  return join(process.env.PASEO_HOME ? expandHome(process.env.PASEO_HOME) : join(homedir(), ".paseo"), "models", "local-speech");
}

/** Archive path in <root>/.downloads. Paseo keeps it there until the install is complete. */
export function archivePath(root: string, m: LocalModel): string {
  return join(root, ".downloads", basename(new URL(m.url).pathname));
}

function nonEmptyFile(p: string): boolean {
  try { const s = statSync(p); return s.isFile() && s.size > 0; } catch { return false; }
}

function hasFiles(dir: string, files: string[]): boolean {
  return files.every((f) => {
    try { const s = statSync(join(dir, f)); return s.isDirectory() || (s.isFile() && s.size > 0); } catch { return false; }
  });
}

/** Paseo's rule: no archive left in .downloads, and each required file is non-empty (a directory also counts). */
export function isInstalled(root: string, id: string): boolean {
  const m = localModel(id);
  if (!m) return false;
  if (nonEmptyFile(archivePath(root, m))) return false;
  return hasFiles(join(root, m.dir), m.files);
}

export type ModelSource = "thronectl" | "paseo";

export interface ResolvedModel {
  id: string;
  path: string;
  source: ModelSource;
  model: LocalModel;
}

/** Finds an installed model: our dir first, then Paseo's dir. */
export function resolveModel(cfg: Config, id: string): ResolvedModel | undefined {
  const m = localModel(id);
  if (!m) return undefined;
  const ours = modelsDir(cfg);
  if (isInstalled(ours, id)) return { id, path: join(ours, m.dir), source: "thronectl", model: m };
  const paseo = paseoModelsDir();
  if (paseo !== ours && isInstalled(paseo, id)) return { id, path: join(paseo, m.dir), source: "paseo", model: m };
  return undefined;
}

function runTar(args: string[]): Promise<void> {
  return new Promise((resolve, reject) => {
    const child = spawn("tar", args, { stdio: ["ignore", "ignore", "pipe"] });
    let err = "";
    child.stderr.on("data", (d: Buffer) => { err = (err + d.toString()).slice(-400); });
    child.on("error", reject);
    child.on("close", (code) => (code === 0 ? resolve() : reject(new Error(`tar exited ${code}: ${err.trim()}`))));
  });
}

async function sha256File(file: string): Promise<string> {
  const hash = createHash("sha256");
  await pipeline(createReadStream(file), hash);
  return hash.digest("hex");
}

/** Streams the archive to a part file and checks its sha256. The download fails after 60 s with no data. */
async function download(m: LocalModel, archive: string, progress: (got: number, total: number) => void): Promise<void> {
  const ctl = new AbortController();
  let stall = setTimeout(() => ctl.abort(new Error("no data for 60 s")), 60_000);
  const part = `${archive}.part-${process.pid}`;
  try {
    const res = await fetch(m.url, { signal: ctl.signal });
    if (!res.ok || !res.body) throw new Error(`${m.id}: download failed (HTTP ${res.status})`);
    const total = Number(res.headers.get("content-length")) || m.bytes;
    const hash = createHash("sha256");
    let got = 0;
    const tap = new Transform({
      transform(chunk: Buffer, _enc, cb): void {
        clearTimeout(stall);
        stall = setTimeout(() => ctl.abort(new Error("no data for 60 s")), 60_000);
        hash.update(chunk);
        got += chunk.length;
        progress(got, total);
        cb(null, chunk);
      },
    });
    await pipeline(Readable.fromWeb(res.body as unknown as WebReadableStream<Uint8Array>), tap, createWriteStream(part));
    const sum = hash.digest("hex");
    if (sum !== m.sha256) throw new Error(`${m.id}: sha256 mismatch (expected ${m.sha256}, got ${sum})`);
    await rename(part, archive);
  } catch (e) {
    await rm(part, { force: true });
    throw e;
  } finally {
    clearTimeout(stall);
  }
}

/** Downloads, checks and extracts one model into <root>/<dir>. Returns the model path. */
export async function installModel(root: string, id: string, progress: (got: number, total: number) => void = () => {}): Promise<string> {
  const m = localModel(id);
  if (!m) throw new Error(`unknown local speech model: ${id}`);
  const archive = archivePath(root, m);
  const staging = join(root, ".downloads", `${m.dir}.extracting-${process.pid}`);
  const target = join(root, m.dir);
  await mkdir(join(root, ".downloads"), { recursive: true });
  // A complete archive from an interrupted install is reused when its sha256 is correct.
  if (nonEmptyFile(archive) && (await sha256File(archive)) !== m.sha256) await rm(archive, { force: true });
  if (!nonEmptyFile(archive)) await download(m, archive, progress);
  await rm(staging, { recursive: true, force: true });
  await mkdir(staging, { recursive: true });
  try {
    await runTar(["xf", archive, "-C", staging]);
    const extracted = join(staging, m.dir);
    if (!hasFiles(extracted, m.files)) throw new Error(`${id}: the archive does not have ${m.files.join(", ")}`);
    await rm(target, { recursive: true, force: true });
    await rm(archive, { force: true });
    await rename(extracted, target);
  } finally {
    await rm(staging, { recursive: true, force: true });
  }
  return target;
}

export interface InstallProgress {
  id: string;
  got: number;
  total: number;
}

export interface LocalModelState {
  id: string;
  installed: boolean;
  path?: string;
  source: ModelSource | "";
}

export interface LocalVoiceStatus {
  addon: boolean;
  addonError?: string;
  addonVersion?: string;
  platform: string;
  modelsDir: string;
  stt: LocalModelState;
  tts: LocalModelState;
  tar: boolean;
  bzip2: boolean; // tar can read .tar.bz2 (GNU tar on Linux needs the bzip2 binary)
  installing: boolean;
  progress?: InstallProgress;
}

let installing: { progress?: InstallProgress } | undefined;

function modelState(cfg: Config, id: string): LocalModelState {
  const r = resolveModel(cfg, id);
  return r ? { id, installed: true, path: r.path, source: r.source } : { id, installed: false, source: "" };
}

function bzip2Ok(): boolean {
  return platform() !== "linux" || which("bzip2") !== undefined;
}

/** Local speech state for the CLI and the RPC layer. */
export function localVoiceStatus(cfg: Config): LocalVoiceStatus {
  const addon = localAddonOk();
  return {
    addon: addon.ok,
    ...(addon.error ? { addonError: addon.error } : {}),
    ...(addon.version ? { addonVersion: addon.version } : {}),
    platform: `${process.platform}-${process.arch}`,
    modelsDir: modelsDir(cfg),
    stt: modelState(cfg, cfg.voice.local.stt),
    tts: modelState(cfg, cfg.voice.local.tts),
    tar: which("tar") !== undefined,
    bzip2: bzip2Ok(),
    installing: installing !== undefined,
    ...(installing?.progress ? { progress: { ...installing.progress } } : {}),
  };
}

function checkKind(id: string, kind: LocalModelKind): void {
  if (localModel(id)?.kind !== kind) {
    throw new Error(`unknown ${kind.toUpperCase()} model "${id}" (choose: ${localModelIds(kind).join(", ")})`);
  }
}

/** Installs the configured STT and TTS models that are missing, one at a time. */
export async function installLocalVoice(
  cfg: Config, onProgress: (p: InstallProgress) => void = () => {},
): Promise<{ installed: string[]; skipped: string[] }> {
  if (installing) throw new Error("an install is already running");
  const state: { progress?: InstallProgress } = {};
  installing = state;
  try {
    const { stt, tts } = cfg.voice.local;
    checkKind(stt, "stt");
    checkKind(tts, "tts");
    const addon = localAddonOk();
    if (!addon.ok) throw new Error(`sherpa-onnx-node is not available on ${process.platform}-${process.arch}: ${addon.error}`);
    if (!which("tar")) throw new Error("tar is not installed");
    if (!bzip2Ok()) throw new Error("bzip2 is not installed (tar needs it to read .tar.bz2)");
    const out = { installed: [] as string[], skipped: [] as string[] };
    for (const id of [stt, tts]) {
      if (resolveModel(cfg, id)) { out.skipped.push(id); continue; }
      let last = 0;
      await installModel(modelsDir(cfg), id, (got, total) => {
        state.progress = { id, got, total };
        const now = Date.now();
        if (now - last >= 200 || got >= total) { last = now; onProgress({ id, got, total }); }
      });
      out.installed.push(id);
    }
    return out;
  } finally {
    installing = undefined;
  }
}
