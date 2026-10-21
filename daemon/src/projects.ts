import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { homedir } from "node:os";
import { basename, join } from "node:path";
import type { Config } from "./config.js";
import { expandHome, shortenHome } from "./paths.js";

export interface Project {
  path: string;
  name: string;
  branch: string;
}

function gitBranch(dir: string): string {
  try {
    let gitDir = join(dir, ".git");
    if (statSync(gitDir).isFile()) {
      const m = /gitdir:\s*(.+)/.exec(readFileSync(gitDir, "utf8"));
      if (!m) return "";
      gitDir = m[1].trim();
    }
    const head = readFileSync(join(gitDir, "HEAD"), "utf8").trim();
    return head.startsWith("ref: refs/heads/") ? head.slice(16) : head.slice(0, 7);
  } catch {
    return "";
  }
}

function mtime(p: string): number {
  try { return statSync(p).mtimeMs; } catch { return 0; }
}

/** Git repos one level under each project root, plus pinned and recently used folders. */
export function listProjects(cfg: Config, recent: string[]): Project[] {
  const seen = new Map<string, number>();
  const add = (p: string, boost = 0): void => {
    const full = expandHome(p);
    if (!existsSync(full) || seen.has(full)) return;
    seen.set(full, mtime(join(full, ".git")) + boost);
  };
  for (const p of cfg.projects) add(p, 1e13);
  for (const p of recent) add(p, 1e12);
  for (const root of cfg.projectRoots) {
    const dir = expandHome(root);
    if (!existsSync(dir)) continue;
    let entries: string[] = [];
    try { entries = readdirSync(dir); } catch { continue; }
    for (const e of entries) {
      if (e.startsWith(".")) continue;
      const full = join(dir, e);
      if (existsSync(join(full, ".git"))) add(full);
    }
  }
  const projects = [...seen.entries()].sort((a, b) => b[1] - a[1]).slice(0, 60).map(([p]) => ({
    path: shortenHome(p), name: basename(p), branch: gitBranch(p),
  }));
  if (!projects.some((p) => p.path === "~")) projects.push({ path: "~", name: basename(homedir()) + " (home)", branch: "" });
  return projects;
}
