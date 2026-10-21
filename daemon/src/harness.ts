import type { HarnessOverride } from "./config.js";
import { which } from "./env.js";
import { rgb565, type HarnessInfo } from "./agents/types.js";

export interface ModeDef {
  id: string;
  label: string;
  args: string[];
}

export interface HarnessDef {
  id: string;
  name: string;
  bins: string[];
  color: string;
  baseArgs: string[];
  modes: ModeDef[];
  /** Args that pass the first prompt. Returns null when the prompt must be typed after start. */
  promptArgs: (prompt: string) => string[] | null;
  sessionArgs?: (uuid: string) => string[];
  interrupt: string;
  permitYes: string;
  permitNo: string;
}

const positional = (p: string): string[] => [p];
const ask = { id: "default", label: "Ask" };

function def(d: Partial<HarnessDef> & Pick<HarnessDef, "id" | "name" | "bins" | "color">): HarnessDef {
  return {
    baseArgs: [], modes: [{ ...ask, args: [] }], promptArgs: positional,
    interrupt: "\x1b", permitYes: "\r", permitNo: "\x1b", ...d,
  };
}

export const BUILTIN_HARNESSES: HarnessDef[] = [
  def({
    id: "claude", name: "Claude Code", bins: ["claude"], color: "#D97757",
    modes: [
      { ...ask, args: [] },
      { id: "accept", label: "Accept edits", args: ["--permission-mode", "acceptEdits"] },
      { id: "plan", label: "Plan", args: ["--permission-mode", "plan"] },
      { id: "bypass", label: "Bypass", args: ["--dangerously-skip-permissions"] },
    ],
    sessionArgs: (uuid) => ["--session-id", uuid],
  }),
  def({
    id: "codex", name: "Codex", bins: ["codex"], color: "#10A37F",
    modes: [
      { ...ask, args: [] },
      { id: "auto", label: "Full auto", args: ["--full-auto"] },
      { id: "yolo", label: "YOLO", args: ["--dangerously-bypass-approvals-and-sandbox"] },
    ],
  }),
  def({ id: "pi", name: "Pi", bins: ["pi"], color: "#C792EA", interrupt: "\x1b" }),
  def({
    id: "cursor", name: "Cursor", bins: ["cursor-agent", "agent"], color: "#7FD1FF",
    modes: [{ ...ask, args: [] }, { id: "force", label: "Force", args: ["--force"] }],
  }),
  def({
    id: "antigravity", name: "Antigravity", bins: ["agy", "antigravity"], color: "#4285F4",
    modes: [
      { ...ask, args: [] },
      { id: "auto", label: "Auto", args: ["--mode", "auto"] },
      { id: "plan", label: "Plan", args: ["--mode", "plan"] },
      { id: "bypass", label: "Bypass", args: ["--dangerously-skip-permissions"] },
    ],
    promptArgs: (p) => ["--prompt-interactive", p],
  }),
  def({
    id: "gemini", name: "Gemini CLI", bins: ["gemini"], color: "#8E75FF",
    modes: [{ ...ask, args: [] }, { id: "auto_edit", label: "Auto edit", args: ["--approval-mode", "auto_edit"] }, { id: "yolo", label: "YOLO", args: ["--yolo"] }],
    promptArgs: (p) => ["-i", p],
  }),
  def({
    id: "opencode", name: "OpenCode", bins: ["opencode"], color: "#FAB283",
    modes: [{ ...ask, args: [] }, { id: "auto", label: "Auto", args: ["--auto"] }],
    promptArgs: (p) => ["--prompt", p],
  }),
  def({ id: "amp", name: "Amp", bins: ["amp"], color: "#F34E3F", promptArgs: () => null, interrupt: "\x03" }),
  def({
    id: "vibe", name: "Mistral Vibe", bins: ["vibe"], color: "#FA520F",
    modes: [{ ...ask, args: [] }, { id: "auto", label: "Auto approve", args: ["--auto-approve"] }],
  }),
  def({ id: "grok", name: "Grok", bins: ["grok"], color: "#E6E6E6" }),
  def({
    id: "junie", name: "Junie", bins: ["junie"], color: "#47E054",
    modes: [{ ...ask, args: [] }, { id: "brave", label: "Brave", args: ["--brave"] }, { id: "plan", label: "Plan", args: ["--plan"] }],
    promptArgs: (p) => [`--prompt=${p}`],
  }),
  def({ id: "kiro", name: "Kiro", bins: ["kiro-cli"], color: "#9046FF", baseArgs: ["chat"] }),
  def({ id: "q", name: "Amazon Q", bins: ["q"], color: "#FF9900", baseArgs: ["chat"] }),
];

export interface ResolvedHarness extends HarnessDef {
  path?: string;
}

export function resolveHarnesses(overrides: Record<string, HarnessOverride>): ResolvedHarness[] {
  const out: ResolvedHarness[] = [];
  const ids = new Set([...BUILTIN_HARNESSES.map((h) => h.id), ...Object.keys(overrides)]);
  for (const id of ids) {
    const base = BUILTIN_HARNESSES.find((h) => h.id === id);
    const o = overrides[id] ?? {};
    if (o.disabled) continue;
    if (!base && !o.bin) continue;
    const h: HarnessDef = base
      ? { ...base, name: o.name ?? base.name, bins: o.bin ? [o.bin] : base.bins, baseArgs: o.args ?? base.baseArgs }
      : def({ id, name: o.name ?? id, bins: [o.bin!], color: "#B0B0B0", baseArgs: o.args ?? [] });
    const path = h.bins.map((b) => which(b)).find(Boolean);
    out.push({ ...h, path });
  }
  return out;
}

export function harnessInfo(h: ResolvedHarness): HarnessInfo {
  return {
    id: `direct:${h.id}`, name: h.name, backend: "direct", ok: !!h.path, color: rgb565(h.color),
    modes: h.modes.map((m) => ({ id: m.id, label: m.label })), mode: h.modes[0].id,
  };
}

export function harnessColor(id: string): number {
  const h = BUILTIN_HARNESSES.find((x) => x.id === id);
  return rgb565(h?.color ?? "#B0B0B0");
}
