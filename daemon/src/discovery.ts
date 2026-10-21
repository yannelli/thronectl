import { createSocket, type Socket } from "node:dgram";
import { logger } from "./log.js";

const log = logger("discovery");
export const PROBE = "THRONE?1";

export interface Beacon {
  t: "throne";
  v: 1;
  id: string;
  name: string;
  port: number;
  os: string;
}

/** Answers UDP broadcast probes from devices on the LAN. */
export function startDiscovery(port: number, beacon: () => Beacon): Socket {
  const sock = createSocket({ type: "udp4", reuseAddr: true });
  sock.on("message", (msg, rinfo) => {
    if (msg.toString("ascii").trim() !== PROBE) return;
    const reply = Buffer.from(JSON.stringify(beacon()));
    sock.send(reply, rinfo.port, rinfo.address);
    log.debug(`probe from ${rinfo.address}:${rinfo.port}`);
  });
  sock.on("error", (e) => log.warn("udp error", e));
  sock.bind(port, () => log.info(`listening for probes on udp/${port}`));
  return sock;
}

/** Sends one probe and collects replies. Used by `thronectl scan` and tests. */
export function probe(port: number, waitMs = 1200, target = "255.255.255.255"): Promise<Beacon[]> {
  return new Promise((resolve) => {
    const found: Beacon[] = [];
    const sock = createSocket("udp4");
    sock.on("message", (msg) => {
      try {
        const b = JSON.parse(msg.toString("utf8")) as Beacon;
        if (b.t === "throne" && !found.some((f) => f.id === b.id)) found.push(b);
      } catch { /* ignore */ }
    });
    sock.bind(0, () => {
      sock.setBroadcast(true);
      sock.send(PROBE, port, target);
    });
    setTimeout(() => { sock.close(); resolve(found); }, waitMs);
  });
}
