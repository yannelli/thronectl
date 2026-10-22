import { execFile, spawn } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { platform, tmpdir } from "node:os";
import { join } from "node:path";
import { apiKey, type Config } from "../config.js";
import { which } from "../env.js";
import { localAddonOk, synthesizeLocal } from "./local/engine.js";
import { resolveModel } from "./local/models.js";
import { decodeWav, resample } from "./wav.js";

export const DEVICE_RATE = 16000;

type TtsProvider = "local" | "say" | "espeak" | "piper" | "openai" | "elevenlabs" | "command";

/** True when sherpa-onnx loads and the configured local TTS model is installed. */
export function localTtsReady(cfg: Config): boolean {
  return localAddonOk().ok && resolveModel(cfg, cfg.voice.local.tts)?.model.kind === "tts";
}

export function pickTts(cfg: Config): TtsProvider | undefined {
  const t = cfg.voice.tts;
  if (t.provider === "off") return undefined;
  if (t.provider !== "auto") return t.provider;
  if (cfg.voice.local.enabled !== false && localTtsReady(cfg)) return "local";
  if (platform() === "darwin" && which("say")) return "say";
  if (apiKey(cfg, "OPENAI_API_KEY")) return "openai";
  if (apiKey(cfg, "ELEVENLABS_API_KEY") && t.voice) return "elevenlabs";
  if (t.model && which("piper")) return "piper";
  if (which("espeak-ng") || which("espeak")) return "espeak";
  return undefined;
}

export function describeTts(cfg: Config): string {
  const p = pickTts(cfg);
  if (p !== "local") return p ?? "none (run thronectl voice install, install espeak-ng or set OPENAI_API_KEY)";
  const id = cfg.voice.local.tts;
  const r = resolveModel(cfg, id);
  if (!r) return `local (${id}, not installed: run thronectl voice install)`;
  const a = localAddonOk();
  return a.ok ? `local (${id}${r.source === "paseo" ? ", from Paseo" : ""})` : `local (${id}, sherpa-onnx-node not available: ${a.error})`;
}

/** Makes agent output speakable: drops code blocks and markdown, cuts at a sentence boundary. */
export function speakable(text: string, maxChars: number): string {
  let s = text
    .replace(/```[\s\S]*?```/g, " (code omitted) ")
    .replace(/`([^`]+)`/g, "$1")
    .replace(/\[([^\]]+)\]\([^)]+\)/g, "$1")
    .replace(/https?:\/\/\S+/g, "a link")
    .replace(/^\s*[#>*\-+|]+\s*/gm, "")
    .replace(/[*_~#|]/g, "")
    .replace(/\s+/g, " ")
    .trim();
  if (s.length > maxChars) {
    const cut = s.slice(0, maxChars);
    const end = Math.max(cut.lastIndexOf(". "), cut.lastIndexOf("! "), cut.lastIndexOf("? "));
    s = end > maxChars * 0.4 ? cut.slice(0, end + 1) : cut + "…";
  }
  return s;
}

function run(cmd: string, args: string[], input?: string): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { stdio: ["pipe", "pipe", "pipe"] });
    const out: Buffer[] = [];
    let err = "";
    const timer = setTimeout(() => child.kill("SIGKILL"), 60_000);
    child.stdout.on("data", (d: Buffer) => out.push(d));
    child.stderr.on("data", (d: Buffer) => { err += d.toString(); });
    child.on("error", reject);
    child.on("close", (code) => {
      clearTimeout(timer);
      if (code === 0) resolve(Buffer.concat(out));
      else reject(new Error(`${cmd} exited ${code}: ${err.trim().slice(0, 160)}`));
    });
    child.stdin.end(input ?? "");
  });
}

async function viaFile(fn: (out: string) => Promise<unknown>): Promise<{ pcm: Buffer; rate: number }> {
  const dir = mkdtempSync(join(tmpdir(), "thronectl-tts-"));
  try {
    const out = join(dir, "speech.wav");
    await fn(out);
    return decodeWav(readFileSync(out));
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
}

async function postAudio(url: string, headers: Record<string, string>, body: unknown): Promise<Buffer> {
  const res = await fetch(url, {
    method: "POST", headers: { "Content-Type": "application/json", ...headers },
    body: JSON.stringify(body), signal: AbortSignal.timeout(45_000),
  });
  if (!res.ok) throw new Error(`TTS ${res.status}: ${(await res.text()).slice(0, 160)}`);
  return Buffer.from(await res.arrayBuffer());
}

/** Streams speech as 16 kHz mono PCM16: per sentence for local, one buffer for other providers. Stops when onChunk returns false. */
export async function synthesizeStream(cfg: Config, text: string, voice: string | undefined, onChunk: (pcm16k: Buffer) => boolean): Promise<void> {
  if (pickTts(cfg) === "local") return synthesizeLocal(cfg, text, voice ?? "", onChunk);
  const pcm = await synthesize(cfg, text, voice);
  if (pcm.length) onChunk(pcm);
}

/** Synthesizes speech as 16 kHz mono PCM16 for the device speaker. */
export async function synthesize(cfg: Config, text: string, voice?: string): Promise<Buffer> {
  const t = cfg.voice.tts;
  const provider = pickTts(cfg);
  if (!provider) throw new Error("No text-to-speech provider configured");
  if (provider === "local") {
    const chunks: Buffer[] = [];
    await synthesizeLocal(cfg, text, voice ?? "", (pcm) => { chunks.push(pcm); return true; });
    return Buffer.concat(chunks);
  }
  const v = voice || t.voice || "";
  let audio: { pcm: Buffer; rate: number };
  switch (provider) {
    case "say":
      audio = await viaFile((out) => run("say", [...(v ? ["-v", v] : []), "-o", out, "--file-format=WAVE", `--data-format=LEI16@${DEVICE_RATE}`, text]));
      break;
    case "espeak": {
      const bin = which("espeak-ng") ? "espeak-ng" : "espeak";
      audio = decodeWav(await run(bin, [...(v ? ["-v", v] : []), "--stdout", text]));
      break;
    }
    case "piper":
      audio = await viaFile((out) => run("piper", ["--model", t.model!, "--output_file", out], text));
      break;
    case "openai": {
      const pcm = await postAudio("https://api.openai.com/v1/audio/speech", { Authorization: `Bearer ${apiKey(cfg, "OPENAI_API_KEY")}` },
        { model: t.model || "gpt-4o-mini-tts", voice: v || "alloy", input: text, response_format: "pcm" });
      audio = { pcm, rate: 24000 };
      break;
    }
    case "elevenlabs": {
      const pcm = await postAudio(`https://api.elevenlabs.io/v1/text-to-speech/${encodeURIComponent(v)}?output_format=pcm_16000`,
        { "xi-api-key": apiKey(cfg, "ELEVENLABS_API_KEY") ?? "" }, { text, model_id: t.model || "eleven_flash_v2_5" });
      audio = { pcm, rate: 16000 };
      break;
    }
    case "command": {
      const tpl = t.command ?? "";
      const quoted = JSON.stringify(text);
      if (tpl.includes("{out}")) {
        audio = await viaFile((out) => new Promise((res, rej) => execFile("/bin/sh", ["-c", tpl.replaceAll("{text}", quoted).replaceAll("{out}", JSON.stringify(out))], { timeout: 60_000 }, (e) => (e ? rej(e) : res(null)))));
      } else {
        audio = decodeWav(await run("/bin/sh", ["-c", tpl.replaceAll("{text}", quoted)]));
      }
      break;
    }
  }
  return resample(audio.pcm, audio.rate, DEVICE_RATE);
}
