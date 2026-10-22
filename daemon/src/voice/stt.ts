import { execFile } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { apiKey, type Config } from "../config.js";
import { which } from "../env.js";
import { localAddonOk, transcribeLocal } from "./local/engine.js";
import { resolveModel } from "./local/models.js";
import { encodeWav, resample, rms } from "./wav.js";

export type SttProvider = "local" | "openai" | "groq" | "command";

/** True when sherpa-onnx loads and the configured local STT model is installed. */
export function localSttReady(cfg: Config): boolean {
  return localAddonOk().ok && resolveModel(cfg, cfg.voice.local.stt)?.model.kind === "stt";
}

export function pickStt(cfg: Config): SttProvider | undefined {
  const p = cfg.voice.stt.provider;
  if (p === "off") return undefined;
  if (p !== "auto") return p;
  if (cfg.voice.local.enabled !== false && localSttReady(cfg)) return "local";
  if (apiKey(cfg, "OPENAI_API_KEY")) return "openai";
  if (apiKey(cfg, "GROQ_API_KEY")) return "groq";
  if (cfg.voice.stt.command) return "command";
  return undefined;
}

export function describeStt(cfg: Config): string {
  const p = pickStt(cfg);
  if (!p) return "none (run thronectl voice install, set OPENAI_API_KEY or GROQ_API_KEY in ~/.config/thronectl/env, or voice.stt.command)";
  if (p === "local") {
    const id = cfg.voice.local.stt;
    const r = resolveModel(cfg, id);
    if (!r) return `local (${id}, not installed: run thronectl voice install)`;
    const a = localAddonOk();
    return a.ok ? `local (${id}${r.source === "paseo" ? ", from Paseo" : ""})` : `local (${id}, sherpa-onnx-node not available: ${a.error})`;
  }
  return p === "command" ? `command: ${cfg.voice.stt.command}` : `${p} (${sttModel(cfg, p)})`;
}

function sttModel(cfg: Config, p: Exclude<SttProvider, "local">): string {
  return cfg.voice.stt.model || (p === "groq" ? "whisper-large-v3-turbo" : "gpt-4o-mini-transcribe");
}

export async function transcribe(cfg: Config, pcm: Buffer, rate: number): Promise<string> {
  if (pcm.length < rate * 2 * 0.3) return "";
  if (rms(pcm) < 0.002) return "";
  const provider = pickStt(cfg);
  if (!provider) throw new Error("No speech-to-text provider configured");
  if (provider === "local") return transcribeLocal(cfg, resample(pcm, rate, 16000));
  const wav = encodeWav(pcm, rate);
  if (provider === "command") return runCommand(cfg.voice.stt.command!, wav);
  const url = provider === "groq"
    ? "https://api.groq.com/openai/v1/audio/transcriptions"
    : "https://api.openai.com/v1/audio/transcriptions";
  const key = apiKey(cfg, provider === "groq" ? "GROQ_API_KEY" : "OPENAI_API_KEY");
  const form = new FormData();
  form.append("file", new Blob([new Uint8Array(wav)], { type: "audio/wav" }), "speech.wav");
  form.append("model", sttModel(cfg, provider));
  if (cfg.voice.stt.language) form.append("language", cfg.voice.stt.language);
  form.append("response_format", "json");
  const res = await fetch(url, { method: "POST", headers: { Authorization: `Bearer ${key}` }, body: form, signal: AbortSignal.timeout(30_000) });
  if (!res.ok) throw new Error(`${provider} STT ${res.status}: ${(await res.text()).slice(0, 160)}`);
  const body = (await res.json()) as { text?: string };
  return (body.text ?? "").trim();
}

/** Runs a local STT command. `{wav}` in the template is replaced with the audio file path. */
function runCommand(template: string, wav: Buffer): Promise<string> {
  const dir = mkdtempSync(join(tmpdir(), "thronectl-stt-"));
  const file = join(dir, "speech.wav");
  writeFileSync(file, wav);
  const cmd = template.includes("{wav}") ? template.replaceAll("{wav}", JSON.stringify(file)) : `${template} ${JSON.stringify(file)}`;
  const shell = which("bash") ?? "/bin/sh";
  return new Promise((resolve, reject) => {
    execFile(shell, ["-c", cmd], { timeout: 120_000, maxBuffer: 4 << 20 }, (err, out) => {
      rmSync(dir, { recursive: true, force: true });
      if (err) reject(new Error(`STT command failed: ${err.message}`));
      else resolve(out.toString().replace(/\[[\d:.]+ --> [\d:.]+\]\s*/g, "").replace(/\s+/g, " ").trim());
    });
  });
}
