import { Duplex } from "node:stream";
import type { Stream } from "../session.js";

/** One open GATT connection, as the radio backend sees it. Packets carry raw stream bytes, at most `mtu` each. */
export interface BleLink {
  readonly mtu: number;
  write(packet: Buffer): Promise<void>;
  onData(cb: (packet: Buffer) => void): void;
  onClose(cb: () => void): void;
  close(): void;
}

/** A byte stream over a BLE link: writes are split into packets and sent in order. */
export function blePipe(link: BleLink, name: string): Stream {
  let closed = false;
  const pipe = new Duplex({
    highWaterMark: 64 * 1024,
    read() { /* data is pushed as packets arrive */ },
    write(chunk: Buffer, _enc, done) {
      (async () => {
        for (let off = 0; off < chunk.length && !closed;) {
          const packet = chunk.subarray(off, off + link.mtu);
          await link.write(packet);
          off += packet.length;
        }
      })().then(() => done(), (e: Error) => done(e));
    },
    final(done) { link.close(); done(); },
    destroy(err, done) {
      if (!closed) { closed = true; link.close(); }
      done(err);
    },
  }) as Stream;
  pipe.remoteAddress = name;
  link.onData((p) => { if (!closed) pipe.push(p); });
  link.onClose(() => {
    if (closed) return;
    closed = true;
    pipe.push(null);
    pipe.destroy();
  });
  return pipe;
}

/** Two linked in-memory ends with a small MTU, for tests and the simulator. */
export function fakeRadio(mtu = 20): [BleLink, BleLink] {
  type End = { data?: (p: Buffer) => void; close: (() => void)[] };
  const ends: [End, End] = [{ close: [] }, { close: [] }];
  let open = true;
  const shut = (): void => {
    if (!open) return;
    open = false;
    for (const e of ends) for (const cb of e.close) setImmediate(cb);
  };
  const make = (me: End, peer: End): BleLink => ({
    mtu,
    write: async (p) => {
      if (!open) throw new Error("link closed");
      if (p.length > mtu) throw new Error(`packet of ${p.length} bytes is over the MTU`);
      const copy = Buffer.from(p);
      await new Promise<void>((r) => setImmediate(r));
      if (open) peer.data?.(copy);
    },
    onData: (cb) => { me.data = cb; },
    onClose: (cb) => { me.close.push(cb); },
    close: shut,
  });
  return [make(ends[0], ends[1]), make(ends[1], ends[0])];
}
