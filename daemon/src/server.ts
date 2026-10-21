import { createServer, type Server, type Socket } from "node:net";
import type { Socket as UdpSocket } from "node:dgram";
import { execFile } from "node:child_process";
import { arch, hostname, platform, userInfo } from "node:os";
import { AgentManager } from "./agents/manager.js";
import { loadConfig, type Config } from "./config.js";
import { controlConnect, startControlServer } from "./control.js";
import { startDiscovery } from "./discovery.js";
import { DeviceStore } from "./identity.js";
import { loadIdentity, type HostIdentity } from "./identity.js";
import { logger } from "./log.js";
import { formatCode, PairingBroker, type PairRequest } from "./pairing.js";
import { VERSION } from "./paths.js";
import { DeviceSession, type HostInfo } from "./rpc.js";
import { Conn, serverHandshake } from "./session.js";
import { StatsSampler, type Stats } from "./stats.js";
import { describeStt } from "./voice/stt.js";
import { describeTts } from "./voice/tts.js";
import { which } from "./env.js";

const log = logger("daemon");

export class Daemon {
  cfg: Config;
  readonly identity: HostIdentity;
  readonly devices = new DeviceStore();
  readonly broker = new PairingBroker();
  readonly agents: AgentManager;
  readonly sessions = new Set<DeviceSession>();
  private readonly sampler: StatsSampler;
  private recent: string[] = [];
  private tcp?: Server;
  private udp?: UdpSocket;
  private control?: Server;
  private statsTimer?: NodeJS.Timeout;

  constructor() {
    this.cfg = loadConfig();
    this.identity = loadIdentity();
    this.agents = new AgentManager(() => this.cfg);
    this.sampler = new StatsSampler(() => this.agents.counts());
    this.agents.on("agents", (list) => this.broadcast("agents", "agents", { agents: list }));
    this.agents.on("notify", (ev) => this.broadcast("notify", "notify", ev));
    this.broker.on("request", (r: PairRequest) => this.desktopNotify(r));
  }

  hostInfo(): HostInfo {
    return {
      id: this.identity.id, name: this.cfg.name, hostname: hostname(), os: platform(), arch: arch(),
      user: userInfo().username, ver: VERSION, paseo: this.agents.paseo.available(),
      tzOff: -new Date().getTimezoneOffset(), time: Date.now(),
    };
  }

  async start(): Promise<void> {
    const live = await controlConnect().then((s) => { s.end(); return true; }, () => false);
    if (live) throw new Error("another thronectl daemon is already running (see: thronectl status)");
    this.tcp = createServer((sock) => void this.accept(sock));
    await new Promise<void>((resolve, reject) => {
      this.tcp!.once("error", reject);
      this.tcp!.listen(this.cfg.port, this.cfg.bind, () => resolve());
    });
    log.info(`listening on tcp/${this.cfg.port} as "${this.cfg.name}" (host id ${this.identity.id})`);
    if (this.cfg.discovery) {
      this.udp = startDiscovery(this.cfg.port, () => ({ t: "throne", v: 1, id: this.identity.id, name: this.cfg.name, port: this.cfg.port, os: platform() }));
    }
    this.control = startControlServer(this.controlHandlers(), { "pair.watch": (s) => this.watchPairing(s) });
    this.agents.startPolling();
    this.statsTimer = setInterval(() => void this.sampleStats(), Math.max(1000, this.cfg.statsMs));
    void this.sampleStats();
    log.info(`speech-to-text: ${describeStt(this.cfg)}; text-to-speech: ${describeTts(this.cfg)}`);
  }

  stop(): void {
    if (this.statsTimer) clearInterval(this.statsTimer);
    for (const s of this.sessions) s.close();
    this.tcp?.close();
    this.udp?.close();
    this.control?.close();
    this.agents.shutdown();
  }

  private async sampleStats(): Promise<void> {
    if (![...this.sessions].some((s) => s.topics.has("stats")) && this.sampler.latest) return;
    const stats = await this.sampler.sample().catch((e: Error) => { log.warn("stats failed", e); return undefined; });
    if (stats) this.broadcast("stats", "stats", stats);
  }

  private broadcast(topic: string, event: string, data: unknown): void {
    for (const s of this.sessions) if (s.topics.has(topic)) s.event(event, data);
  }

  private async accept(sock: Socket): Promise<void> {
    const addr = sock.remoteAddress?.replace(/^::ffff:/, "") ?? "?";
    sock.setNoDelay(true);
    sock.setKeepAlive(true, 15_000);
    const conn = new Conn(sock);
    let hs;
    try {
      hs = await serverHandshake(conn, this.identity, this.identity.id, this.cfg.name);
    } catch (e) {
      log.warn(`handshake with ${addr} failed`, e);
      sock.destroy();
      return;
    }
    const { channel, device, keys } = hs;
    const trusted = this.devices.findByKey(device.pk);
    if (!trusted || !device.known) {
      log.info(`pairing request from "${device.name}" (${addr}), code ${formatCode(keys.sas)}`);
      channel.sendJson({ t: "auth", state: "pair", code: keys.sas });
      const { result, cancel } = this.broker.request({ deviceId: device.id, deviceName: device.name, addr, code: keys.sas, fw: device.fw });
      channel.once("close", cancel);
      const yes = await result;
      if (!yes || channel.closed) {
        channel.sendJson({ t: "auth", state: "denied" });
        channel.close();
        return;
      }
      this.devices.add({ id: device.id, name: device.name, pk: device.pk.toString("base64"), pairedAt: new Date().toISOString() });
      log.info(`paired "${device.name}" (${device.id})`);
    }
    this.devices.touch(device.id, addr, device.fw);
    channel.sendJson({ t: "auth", state: "ok", host: this.hostInfo() });
    const session = new DeviceSession(channel, device, {
      cfg: () => this.cfg,
      host: () => this.hostInfo(),
      agents: this.agents,
      latestStats: (): Stats | undefined => this.sampler.latest,
      recentCwds: () => this.recent,
      addRecentCwd: (cwd) => { this.recent = [cwd, ...this.recent.filter((c) => c !== cwd)].slice(0, 10); },
    });
    this.sessions.add(session);
    log.info(`"${device.name}" connected from ${addr} (fw ${device.fw || "?"})`);
    const idle = setInterval(() => { if (Date.now() - channel.lastRx > 30_000) channel.close(); }, 5000);
    channel.on("close", () => {
      clearInterval(idle);
      this.sessions.delete(session);
      log.info(`"${device.name}" disconnected`);
    });
  }

  private controlHandlers(): Parameters<typeof startControlServer>[0] {
    return {
      status: () => ({
        host: this.hostInfo(), port: this.cfg.port,
        connected: [...this.sessions].map((s) => ({ id: s.device.id, name: s.device.name, addr: s.ch.remote, fw: s.device.fw, since: s.since })),
        agents: this.agents.counts(), stt: describeStt(this.cfg), tts: describeTts(this.cfg), pending: this.broker.pending(),
      }),
      "devices.list": () => this.devices.list().map((d) => ({ ...d, online: [...this.sessions].some((s) => s.device.id === d.id) })),
      "devices.rm": (args) => {
        const removed = this.devices.remove(String(args.match ?? ""));
        for (const s of this.sessions) if (removed.some((d) => d.id === s.device.id)) s.close();
        return removed;
      },
      "pair.list": () => this.broker.pending(),
      "pair.answer": (args) => {
        if (!this.broker.answer(String(args.id), args.yes === true)) throw new Error("no such pairing request (expired?)");
        return {};
      },
      notify: (args) => {
        const ev = { kind: String(args.kind ?? "info"), title: String(args.title ?? "thronectl"), body: String(args.body ?? "") };
        for (const s of this.sessions) s.event("notify", ev);
        return { sent: this.sessions.size };
      },
      say: async (args) => {
        const targets = [...this.sessions];
        if (!targets.length) throw new Error("no device connected");
        await Promise.all(targets.map((s) => s.say(String(args.text ?? ""))));
        return { sent: targets.length };
      },
      reload: () => {
        this.cfg = loadConfig();
        this.devices.reload();
        return { ok: true };
      },
    };
  }

  private watchPairing(sock: Socket): void {
    const send = (o: unknown): void => { if (!sock.destroyed) sock.write(JSON.stringify(o) + "\n"); };
    for (const r of this.broker.pending()) send({ ev: "pair.request", req: r });
    const onReq = (r: PairRequest): void => send({ ev: "pair.request", req: r });
    const onRes = (r: PairRequest, yes: boolean, reason: string): void => send({ ev: "pair.resolved", id: r.id, yes, reason });
    this.broker.on("request", onReq);
    this.broker.on("resolved", onRes);
    sock.on("close", () => {
      this.broker.off("request", onReq);
      this.broker.off("resolved", onRes);
    });
  }

  private desktopNotify(r: PairRequest): void {
    if (!this.cfg.notify.desktop) return;
    const msg = `${r.deviceName} wants to pair. Code ${formatCode(r.code)}. Run: thronectl pair`;
    if (platform() === "darwin") {
      execFile("osascript", ["-e", `display notification ${JSON.stringify(msg)} with title "thronectl" sound name "Glass"`], () => {});
    } else if (which("notify-send")) {
      execFile("notify-send", ["thronectl", msg], () => {});
    }
  }
}
