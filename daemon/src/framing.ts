import type { Writable } from "node:stream";

export const MAX_FRAME = 65536;

/** Splits a TCP byte stream into length-prefixed frames. */
export class FrameReader {
  private buf: Buffer = Buffer.alloc(0);

  constructor(private readonly onFrame: (frame: Buffer) => void, private readonly onError: (e: Error) => void) {}

  push(chunk: Buffer): void {
    this.buf = this.buf.length === 0 ? chunk : Buffer.concat([this.buf, chunk]);
    while (this.buf.length >= 4) {
      const len = this.buf.readUInt32BE(0);
      if (len > MAX_FRAME) {
        this.onError(new Error(`frame too large: ${len}`));
        this.buf = Buffer.alloc(0);
        return;
      }
      if (this.buf.length < 4 + len) return;
      const frame = this.buf.subarray(4, 4 + len);
      this.buf = this.buf.subarray(4 + len);
      this.onFrame(Buffer.from(frame));
    }
  }
}

export function encodeFrame(payload: Buffer): Buffer {
  if (payload.length > MAX_FRAME) throw new Error(`frame too large: ${payload.length}`);
  const head = Buffer.alloc(4);
  head.writeUInt32BE(payload.length, 0);
  return Buffer.concat([head, payload]);
}

export function writeFrame(sock: Writable, payload: Buffer): boolean {
  return sock.write(encodeFrame(payload));
}
