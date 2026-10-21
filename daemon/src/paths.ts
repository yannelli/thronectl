import { homedir } from "node:os";
import { join } from "node:path";
import { mkdirSync } from "node:fs";

export const VERSION = "0.3.0";
export const DEFAULT_PORT = 4787;

export function configDir(): string {
  const base = process.env.THRONECTL_HOME
    ?? join(process.env.XDG_CONFIG_HOME ?? join(homedir(), ".config"), "thronectl");
  mkdirSync(base, { recursive: true, mode: 0o700 });
  return base;
}

export const paths = {
  config: (): string => join(configDir(), "config.json"),
  identity: (): string => join(configDir(), "identity.json"),
  devices: (): string => join(configDir(), "devices.json"),
  env: (): string => join(configDir(), "env"),
  socket: (): string => join(configDir(), "control.sock"),
  log: (): string => join(configDir(), "daemon.log"),
  pid: (): string => join(configDir(), "daemon.pid"),
};

export function expandHome(p: string): string {
  if (p === "~") return homedir();
  if (p.startsWith("~/")) return join(homedir(), p.slice(2));
  return p;
}

export function shortenHome(p: string): string {
  const home = homedir();
  return p.startsWith(home) ? "~" + p.slice(home.length) : p;
}
