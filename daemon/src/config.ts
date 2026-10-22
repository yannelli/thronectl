import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { hostname } from "node:os";
import { DEFAULT_PORT, paths } from "./paths.js";

export interface HarnessOverride {
  bin?: string;
  args?: string[];
  name?: string;
  disabled?: boolean;
}

export interface SttConfig {
  provider: "auto" | "local" | "openai" | "groq" | "command" | "off";
  model?: string;
  language?: string;
  command?: string;
}

export interface TtsConfig {
  provider: "auto" | "local" | "say" | "espeak" | "piper" | "openai" | "elevenlabs" | "command" | "off";
  voice?: string;
  model?: string;
  command?: string;
  maxChars: number;
}

/** On-host speech with sherpa-onnx, the same models Paseo uses. */
export interface LocalVoiceConfig {
  enabled: boolean; // false: auto skips local speech
  modelsDir?: string; // default ~/.config/thronectl/models/local-speech
  stt: string; // model id, e.g. parakeet-tdt-0.6b-v2-int8
  tts: string; // model id, e.g. kokoro-en-v0_19
  threads: number;
  speed: number;
}

export interface Config {
  name: string;
  port: number;
  bind: string;
  discovery: boolean;
  allowShell: boolean;
  shell: string;
  projectRoots: string[];
  projects: string[];
  paseo: { enabled: "auto" | boolean; bin: string };
  harness: Record<string, HarnessOverride>;
  voice: { stt: SttConfig; tts: TtsConfig; keys: Record<string, string>; local: LocalVoiceConfig };
  notify: { desktop: boolean };
  statsMs: number;
}

export function defaultConfig(): Config {
  return {
    name: hostname().replace(/\.local$/, ""),
    port: DEFAULT_PORT,
    bind: "0.0.0.0",
    discovery: true,
    allowShell: true,
    shell: process.env.SHELL ?? "/bin/zsh",
    projectRoots: ["~/workspace", "~/code", "~/Developer", "~/src", "~/projects", "~/dev", "~/git", "~/repos"],
    projects: [],
    paseo: { enabled: "auto", bin: "paseo" },
    harness: {},
    voice: {
      stt: { provider: "auto", language: "en" },
      tts: { provider: "auto", maxChars: 600 },
      keys: {},
      local: { enabled: true, stt: "parakeet-tdt-0.6b-v2-int8", tts: "kokoro-en-v0_19", threads: 2, speed: 1.0 },
    },
    notify: { desktop: true },
    statsMs: 2000,
  };
}

function merge<T>(base: T, over: unknown): T {
  if (typeof base !== "object" || base === null || Array.isArray(base)) {
    return (over === undefined ? base : over) as T;
  }
  if (typeof over !== "object" || over === null) return base;
  const out: Record<string, unknown> = { ...(base as Record<string, unknown>) };
  for (const [k, v] of Object.entries(over as Record<string, unknown>)) {
    out[k] = k in out ? merge(out[k], v) : v;
  }
  return out as T;
}

export function loadConfig(): Config {
  const file = paths.config();
  if (!existsSync(file)) {
    const cfg = defaultConfig();
    writeFileSync(file, JSON.stringify(cfg, null, 2) + "\n", { mode: 0o600 });
    return cfg;
  }
  const raw = JSON.parse(readFileSync(file, "utf8")) as unknown;
  return merge(defaultConfig(), raw);
}

export function saveConfig(cfg: Config): void {
  writeFileSync(paths.config(), JSON.stringify(cfg, null, 2) + "\n", { mode: 0o600 });
}

/** Sets a dotted key such as `voice.tts.provider`. Values parse as JSON when they can. */
export function setConfigValue(cfg: Config, key: string, value: string): Config {
  let parsed: unknown = value;
  try { parsed = JSON.parse(value); } catch { /* plain string */ }
  const parts = key.split(".");
  let node = cfg as unknown as Record<string, unknown>;
  for (const part of parts.slice(0, -1)) {
    if (typeof node[part] !== "object" || node[part] === null) node[part] = {};
    node = node[part] as Record<string, unknown>;
  }
  node[parts[parts.length - 1]] = parsed;
  return cfg;
}

/** Loads KEY=VALUE lines from ~/.config/thronectl/env into process.env (launchd has no shell env). */
export function loadEnvFile(): void {
  const file = paths.env();
  if (!existsSync(file)) return;
  for (const line of readFileSync(file, "utf8").split("\n")) {
    const m = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)\s*$/.exec(line);
    if (!m || line.trimStart().startsWith("#")) continue;
    const val = m[2].replace(/^(['"])(.*)\1$/, "$2");
    if (process.env[m[1]] === undefined) process.env[m[1]] = val;
  }
}

export function apiKey(cfg: Config, name: string): string | undefined {
  return cfg.voice.keys[name] ?? process.env[name];
}
