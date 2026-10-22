import { chmodSync, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import { BUILTIN_HARNESSES, resolveHarnesses } from "./harness.js";
import { which } from "./env.js";
import { setConfigValue, type Config, type HarnessOverride } from "./config.js";
import { paths } from "./paths.js";
import { LOCAL_MODELS, localModelIds, localVoiceStatus } from "./voice/local/models.js";

/** The daemon settings a paired device may read and change, grouped the way the device shows them. */

export type FieldType = "bool" | "text" | "number" | "choice" | "list" | "secret" | "link" | "add" | "action";

export interface Field {
  key: string;
  label: string;
  type: FieldType;
  value: string | number | boolean;
  options?: string[];
  help?: string;
  restart?: boolean;
  live?: boolean;
  confirm?: string;  // for an action: the device asks first, with this text
}

export const GROUPS = [
  { id: "general", label: "General", sub: "name, shell, stats" },
  { id: "network", label: "Network", sub: "port, bind, discovery" },
  { id: "projects", label: "Projects", sub: "folders to list" },
  { id: "paseo", label: "Paseo", sub: "backend" },
  { id: "voice", label: "Voice", sub: "speech providers" },
  { id: "harnesses", label: "Harnesses", sub: "agent CLIs" },
  { id: "keys", label: "Keys", sub: "API keys and env" },
];

const STT = ["auto", "local", "openai", "groq", "command", "off"];
const TTS = ["auto", "local", "say", "espeak", "piper", "openai", "elevenlabs", "command", "off"];
/** Keys the device lists even when they are not set yet. */
const KNOWN_KEYS = ["OPENAI_API_KEY", "GROQ_API_KEY", "ELEVENLABS_API_KEY", "ANTHROPIC_API_KEY", "GEMINI_API_KEY"];
const ENV_NAME = /^[A-Za-z_][A-Za-z0-9_]{0,63}$/;
const HARNESS_ID = /^[a-z0-9][a-z0-9_-]{0,31}$/;
const HARNESS_KEY = /^harness\.([a-z0-9][a-z0-9_-]{0,31})\.(bin|args|name|disabled)$/;

const get = (cfg: Config, key: string): unknown =>
  key.split(".").reduce<unknown>((o, k) => (o as Record<string, unknown> | undefined)?.[k], cfg);

/** Shows a secret as its first 3 and last 4 characters. */
export function mask(v: string): string {
  return v.length <= 10 ? "*".repeat(v.length) : `${v.slice(0, 3)}...${v.slice(-4)}`;
}

function field(cfg: Config, key: string, label: string, type: FieldType, extra: Partial<Field> = {}): Field {
  const v = get(cfg, key);
  const value = type === "list" ? (Array.isArray(v) ? v.join("\n") : "") : typeof v === "boolean" || typeof v === "number" ? v : String(v ?? "");
  return { key, label, type, value, ...extra };
}

/** Plain fields, without the dynamic harness and key groups. */
function staticFields(cfg: Config, group: string): Field[] {
  switch (group) {
    case "general":
      return [
        field(cfg, "name", "Host name", "text", { help: "What devices show for this host" }),
        field(cfg, "allowShell", "Allow shell", "bool", { help: "Devices may open a login shell" }),
        field(cfg, "shell", "Shell", "text"),
        field(cfg, "statsMs", "Stats interval (ms)", "number"),
        field(cfg, "notify.desktop", "Desktop alerts", "bool", { help: "Pairing requests show on this computer" }),
      ];
    case "network":
      return [
        field(cfg, "port", "Port", "number", { restart: true }),
        field(cfg, "bind", "Bind address", "text", { restart: true }),
        field(cfg, "discovery", "Discovery", "bool", { restart: true, help: "Answer device scans on the LAN" }),
        { key: "restart", label: "Restart daemon", type: "action", value: "", help: "Applies port, bind and discovery", confirm: "Agents keep running. The device reconnects." },
      ];
    case "projects":
      return [
        field(cfg, "projectRoots", "Project roots", "list", { help: "One folder per line. Their subfolders are listed" }),
        field(cfg, "projects", "Extra projects", "list", { help: "One folder per line" }),
      ];
    case "paseo":
      return [
        { ...field(cfg, "paseo.enabled", "Use Paseo", "choice"), value: String(cfg.paseo.enabled), options: ["auto", "true", "false"] },
        field(cfg, "paseo.bin", "Paseo command", "text"),
      ];
    case "voice":
      return [
        ...localVoiceFields(cfg),
        field(cfg, "voice.stt.provider", "Speech to text", "choice", { options: STT }),
        field(cfg, "voice.stt.model", "STT model", "text", { help: "Empty for the provider default" }),
        field(cfg, "voice.stt.language", "STT language", "text"),
        field(cfg, "voice.stt.command", "STT command", "text", { help: "{wav} is the audio file" }),
        field(cfg, "voice.tts.provider", "Text to speech", "choice", { options: TTS }),
        field(cfg, "voice.tts.voice", "TTS voice", "text"),
        field(cfg, "voice.tts.model", "TTS model", "text"),
        field(cfg, "voice.tts.command", "TTS command", "text", { help: "{text} and {out} are filled in" }),
        field(cfg, "voice.tts.maxChars", "Longest reply read", "number"),
        field(cfg, "voice.local.stt", "Local STT model", "choice", { options: localModelIds("stt") }),
        field(cfg, "voice.local.tts", "Local TTS model", "choice", { options: localModelIds("tts") }),
        field(cfg, "voice.local.threads", "Local threads", "number"),
        field(cfg, "voice.local.speed", "Local speech speed", "number"),
      ];
    default:
      return [];
  }
}

const mb = (bytes: number): string => `${Math.round(bytes / 1e6)} MB`;

/** Bytes still to download for the configured local models. */
export function localDownloadBytes(cfg: Config): number {
  const st = localVoiceStatus(cfg);
  return [st.stt, st.tts].filter((m) => !m.installed).reduce((n, m) => n + (LOCAL_MODELS.find((x) => x.id === m.id)?.bytes ?? 0), 0);
}

/** The on-host speech switch and model state, first in the voice group. */
function localVoiceFields(cfg: Config): Field[] {
  const st = localVoiceStatus(cfg);
  const usable = (p: string): boolean => p === "auto" || p === "local";
  const on = cfg.voice.local.enabled !== false && usable(cfg.voice.stt.provider) && usable(cfg.voice.tts.provider);
  const out: Field[] = [{ key: "voice.localOn", label: "Use local voice", type: "bool", value: on, help: "Speech runs on this host, with no API keys" }];
  const p = st.progress;
  let label = "Install local voice", value = mb(localDownloadBytes(cfg)), live = false;
  if (!st.addon) [label, value] = ["Local voice", `not on ${st.platform}`];
  else if (st.installing) [label, value, live] = ["Installing", p ? `${p.id.split("-")[0]} ${Math.floor((p.got / Math.max(1, p.total)) * 100)}%` : "starting", true];
  else if (st.stt.installed && st.tts.installed) [label, value] = ["Local models", st.stt.source === "paseo" || st.tts.source === "paseo" ? "ready (Paseo's)" : "ready"];
  const ask = label === "Install local voice" ? { confirm: `Downloads ${value} to ${st.modelsDir}.` } : {};
  out.push({ key: "voice.install", label, type: "action", value, live, help: "Downloads the models to this host", ...ask });
  return out;
}

function harnessFields(cfg: Config, id: string): Field[] {
  const base = BUILTIN_HARNESSES.find((h) => h.id === id);
  const o = cfg.harness[id] ?? {};
  const bin = o.bin ?? base?.bins[0] ?? "";
  const path = bin ? which(bin) : undefined;
  return [
    { key: `harness.${id}.disabled`, label: "Disabled", type: "bool", value: !!o.disabled },
    { key: `harness.${id}.name`, label: "Name", type: "text", value: o.name ?? base?.name ?? id },
    { key: `harness.${id}.bin`, label: "Command", type: "text", value: o.bin ?? "", help: path ? `Found: ${path}` : `Not on PATH: ${bin || "(none)"}` },
    { key: `harness.${id}.args`, label: "Args", type: "list", value: (o.args ?? base?.baseArgs ?? []).join("\n"), help: "One per line, before the prompt" },
    ...(base ? [] : [{ key: `remove:${id}`, label: "Remove this harness", type: "action" as const, value: "", confirm: "Its settings are deleted from the host." }]),
  ];
}

function harnessLinks(cfg: Config): Field[] {
  const resolved = new Map(resolveHarnesses(cfg.harness).map((h) => [h.id, h]));
  const ids = [...new Set([...BUILTIN_HARNESSES.map((h) => h.id), ...Object.keys(cfg.harness)])];
  const out: Field[] = ids.map((id) => {
    const h = resolved.get(id);
    const state = cfg.harness[id]?.disabled ? "off" : h?.path ? "ready" : "missing";
    return { key: `harness:${id}`, label: h?.name ?? cfg.harness[id]?.name ?? id, type: "link", value: state };
  });
  out.push({ key: "harness", label: "Add a harness", type: "add", value: "", help: "Any CLI that runs in a terminal" });
  return out;
}

/** KEY=VALUE pairs from the env file, in file order. */
export function readEnvFile(): Map<string, string> {
  const out = new Map<string, string>();
  if (!existsSync(paths.env())) return out;
  for (const line of readFileSync(paths.env(), "utf8").split("\n")) {
    const m = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)\s*$/.exec(line);
    if (m && !line.trimStart().startsWith("#")) out.set(m[1], m[2].replace(/^(['"])(.*)\1$/, "$2"));
  }
  return out;
}

function keyFields(): Field[] {
  const file = readEnvFile();
  const names = [...new Set([...KNOWN_KEYS, ...file.keys()])];
  const out: Field[] = names.map((k) => {
    const v = file.get(k) ?? process.env[k];
    const from = file.has(k) ? "" : v ? " (from the daemon's environment)" : "";
    return { key: `env.${k}`, label: k, type: "secret", value: v ? mask(v) + from : "" };
  });
  out.push({ key: "env", label: "Add a variable", type: "add", value: "", help: "Agents and voice providers read these" });
  return out;
}

export function groupFields(cfg: Config, group: string): Field[] {
  if (group === "harnesses") return harnessLinks(cfg);
  if (group === "keys") return keyFields();
  if (group.startsWith("harness:")) {
    const id = group.slice(8);
    if (!HARNESS_ID.test(id)) throw new Error(`bad harness id "${id}"`);
    return harnessFields(cfg, id);
  }
  if (!GROUPS.some((g) => g.id === group)) throw new Error(`no settings group "${group}"`);
  return staticFields(cfg, group);
}

/** The field a key belongs to, or undefined when a device may not set it. */
function fieldFor(cfg: Config, key: string): Field | undefined {
  const h = HARNESS_KEY.exec(key);
  if (h) return harnessFields(cfg, h[1]).find((f) => f.key === key);
  for (const g of GROUPS) {
    const f = staticFields(cfg, g.id).find((x) => x.key === key && x.type !== "action");
    if (f) return f;
  }
  return undefined;
}

/** Applies one change from a device. Returns the new config and whether the daemon must restart. */
export function applyField(cfg: Config, key: string, raw: unknown): { cfg: Config; restart: boolean } {
  const f = fieldFor(cfg, key);
  if (!f) throw new Error(`"${key}" cannot be set from a device`);
  let value: unknown;
  const s = typeof raw === "string" ? raw : String(raw ?? "");
  switch (f.type) {
    case "bool": value = raw === true || s === "true" || s === "1"; break;
    case "number": {
      const n = Number(s);
      if (!Number.isFinite(n)) throw new Error(`${f.label} must be a number`);
      value = n;
      break;
    }
    case "choice":
      if (!f.options?.includes(s)) throw new Error(`${f.label} must be one of: ${f.options?.join(", ")}`);
      value = key === "paseo.enabled" && s !== "auto" ? s === "true" : s;
      break;
    case "list": value = s.split(/\n|,/).map((x) => x.trim()).filter(Boolean); break;
    default: value = s;
  }
  // On or off, auto picks the provider: local first when it is on, then API keys and system voices.
  if (key === "voice.localOn") {
    cfg.voice.local.enabled = value === true;
    cfg.voice.stt.provider = "auto";
    cfg.voice.tts.provider = "auto";
    return { cfg, restart: false };
  }
  // An empty value removes the override, so the built-in default applies again.
  const h = HARNESS_KEY.exec(key);
  if (h) {
    const o: Record<string, unknown> = { ...(cfg.harness[h[1]] ?? {}) };
    if (value === "" || value === false || (Array.isArray(value) && !value.length)) delete o[h[2]];
    else o[h[2]] = value;
    cfg.harness[h[1]] = o as HarnessOverride;
    return { cfg, restart: false };
  }
  return { cfg: setConfigValue(cfg, key, JSON.stringify(value)), restart: !!f.restart };
}

/** Sets or removes one env file entry and applies it to this process, so new agents get it. */
export function setEnvVar(name: string, value: string | undefined): void {
  if (!ENV_NAME.test(name)) throw new Error(`"${name}" is not a valid variable name`);
  if (value !== undefined && /[\r\n]/.test(value)) throw new Error("the value must be one line");
  const file = paths.env();
  const lines = existsSync(file) ? readFileSync(file, "utf8").split("\n") : [];
  const at = lines.findIndex((l) => new RegExp(`^\\s*${name}\\s*=`).test(l));
  const old = readEnvFile().get(name);
  // Plain KEY=value, since loadEnvFile strips quotes but does not unescape.
  const entry = value === undefined ? undefined : `${name}=${value}`;
  if (at >= 0 && entry) lines[at] = entry;
  else if (at >= 0) lines.splice(at, 1);
  else if (entry) {
    if (lines.length && lines[lines.length - 1] === "") lines.pop();
    lines.push(entry, "");
  }
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(file, lines.join("\n"), { mode: 0o600 });
  chmodSync(file, 0o600);
  if (value !== undefined) process.env[name] = value;
  else if (old !== undefined && process.env[name] === old) delete process.env[name];
}

export function addHarness(cfg: Config, id: string, bin: string): Config {
  if (!HARNESS_ID.test(id)) throw new Error("use lower-case letters, digits, - and _ for the id");
  if (BUILTIN_HARNESSES.some((h) => h.id === id) || cfg.harness[id]) throw new Error(`a harness "${id}" exists already`);
  if (!bin.trim()) throw new Error("give the command to run");
  cfg.harness[id] = { bin: bin.trim(), name: id };
  return cfg;
}

export function removeHarness(cfg: Config, id: string): Config {
  if (BUILTIN_HARNESSES.some((h) => h.id === id)) throw new Error("built-in harnesses can only be disabled");
  delete cfg.harness[id];
  return cfg;
}
