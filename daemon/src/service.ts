import { execFileSync } from "node:child_process";
import { existsSync, mkdirSync, rmSync, writeFileSync } from "node:fs";
import { homedir, platform, userInfo } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import { paths } from "./paths.js";

const LABEL = "dev.thronectl.daemon";

function cliPath(): string {
  return fileURLToPath(new URL("./cli.js", import.meta.url));
}

const xml = (s: string): string => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

function plist(): string {
  const args = [process.execPath, cliPath(), "daemon"].map((a) => `    <string>${xml(a)}</string>`).join("\n");
  return `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>${LABEL}</string>
  <key>ProgramArguments</key>
  <array>
${args}
  </array>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ProcessType</key><string>Interactive</string>
  <key>StandardOutPath</key><string>${xml(paths.log())}</string>
  <key>StandardErrorPath</key><string>${xml(paths.log())}</string>
  <key>EnvironmentVariables</key>
  <dict>
    <key>PATH</key><string>${xml(process.env.PATH ?? "/usr/bin:/bin")}</string>
    <key>SHELL</key><string>${xml(process.env.SHELL ?? "/bin/zsh")}</string>
  </dict>
</dict>
</plist>
`;
}

function unit(): string {
  return `[Unit]
Description=thronectl host daemon
After=network-online.target

[Service]
ExecStart=${process.execPath} ${cliPath()} daemon
Restart=always
RestartSec=3
Environment=PATH=${process.env.PATH ?? "/usr/bin:/bin"}
Environment=SHELL=${process.env.SHELL ?? "/bin/bash"}

[Install]
WantedBy=default.target
`;
}

const plistPath = (): string => join(homedir(), "Library", "LaunchAgents", `${LABEL}.plist`);
const unitPath = (): string => join(homedir(), ".config", "systemd", "user", "thronectl.service");

export function installService(): string {
  if (platform() === "darwin") {
    const file = plistPath();
    mkdirSync(join(homedir(), "Library", "LaunchAgents"), { recursive: true });
    const domain = `gui/${userInfo().uid}`;
    try { execFileSync("launchctl", ["bootout", `${domain}/${LABEL}`], { stdio: "ignore" }); } catch { /* not loaded */ }
    writeFileSync(file, plist());
    execFileSync("launchctl", ["bootstrap", domain, file]);
    return `launchd agent installed: ${file}\nlogs: ${paths.log()}`;
  }
  if (platform() === "linux") {
    const file = unitPath();
    mkdirSync(join(homedir(), ".config", "systemd", "user"), { recursive: true });
    writeFileSync(file, unit());
    execFileSync("systemctl", ["--user", "daemon-reload"]);
    execFileSync("systemctl", ["--user", "enable", "--now", "thronectl.service"]);
    return `systemd user service installed: ${file}\nlogs: journalctl --user -u thronectl -f\nTo keep it running after logout: loginctl enable-linger ${userInfo().username}`;
  }
  throw new Error(`service install is not supported on ${platform()}`);
}

/** Restarts the login service so it runs the new build. Undefined when no service is installed. */
/** True when `thronectl install` set up a login service that restartService can restart. */
export function serviceInstalled(): boolean {
  return (platform() === "darwin" && existsSync(plistPath())) || (platform() === "linux" && existsSync(unitPath()));
}

export function restartService(): string | undefined {
  if (platform() === "darwin" && existsSync(plistPath())) {
    execFileSync("launchctl", ["kickstart", "-k", `gui/${userInfo().uid}/${LABEL}`]);
    return "Restarted the launchd agent.";
  }
  if (platform() === "linux" && existsSync(unitPath())) {
    execFileSync("systemctl", ["--user", "restart", "thronectl.service"]);
    return "Restarted the systemd service.";
  }
  return undefined;
}

export function uninstallService(): string {
  if (platform() === "darwin") {
    try { execFileSync("launchctl", ["bootout", `gui/${userInfo().uid}/${LABEL}`], { stdio: "ignore" }); } catch { /* not loaded */ }
    if (existsSync(plistPath())) rmSync(plistPath());
    return "launchd agent removed";
  }
  if (platform() === "linux") {
    try { execFileSync("systemctl", ["--user", "disable", "--now", "thronectl.service"], { stdio: "ignore" }); } catch { /* not installed */ }
    if (existsSync(unitPath())) rmSync(unitPath());
    return "systemd user service removed";
  }
  throw new Error(`service install is not supported on ${platform()}`);
}
