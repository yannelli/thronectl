/** PCM16 mono helpers. */

export function encodeWav(pcm: Buffer, rate: number): Buffer {
  const h = Buffer.alloc(44);
  h.write("RIFF", 0);
  h.writeUInt32LE(36 + pcm.length, 4);
  h.write("WAVE", 8);
  h.write("fmt ", 12);
  h.writeUInt32LE(16, 16);
  h.writeUInt16LE(1, 20);
  h.writeUInt16LE(1, 22);
  h.writeUInt32LE(rate, 24);
  h.writeUInt32LE(rate * 2, 28);
  h.writeUInt16LE(2, 32);
  h.writeUInt16LE(16, 34);
  h.write("data", 36);
  h.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([h, pcm]);
}

/** Decodes a PCM WAV (8/16/24/32-bit int, any channel count) to mono PCM16. */
export function decodeWav(wav: Buffer): { pcm: Buffer; rate: number } {
  if (wav.toString("ascii", 0, 4) !== "RIFF" || wav.toString("ascii", 8, 12) !== "WAVE") throw new Error("not a WAV file");
  let off = 12, rate = 16000, channels = 1, bits = 16, fmt = 1;
  while (off + 8 <= wav.length) {
    const id = wav.toString("ascii", off, off + 4);
    let size = wav.readUInt32LE(off + 4);
    const body = off + 8;
    if (id === "fmt ") {
      fmt = wav.readUInt16LE(body);
      channels = wav.readUInt16LE(body + 2);
      rate = wav.readUInt32LE(body + 4);
      bits = wav.readUInt16LE(body + 14);
    } else if (id === "data") {
      if (size === 0xffffffff || body + size > wav.length) size = wav.length - body;
      if (fmt !== 1 && fmt !== 0xfffe) throw new Error(`unsupported WAV format ${fmt}`);
      return { pcm: toMono16(wav.subarray(body, body + size), channels, bits), rate };
    }
    off = body + size + (size & 1);
  }
  throw new Error("WAV has no data chunk");
}

function toMono16(data: Buffer, channels: number, bits: number): Buffer {
  const bps = bits / 8;
  const frames = Math.floor(data.length / (bps * channels));
  const out = Buffer.alloc(frames * 2);
  for (let i = 0; i < frames; i++) {
    let sum = 0;
    for (let c = 0; c < channels; c++) {
      const o = (i * channels + c) * bps;
      if (bits === 16) sum += data.readInt16LE(o);
      else if (bits === 8) sum += (data[o] - 128) << 8;
      else if (bits === 24) sum += data.readIntLE(o, 3) >> 8;
      else sum += data.readInt32LE(o) >> 16;
    }
    out.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(sum / channels))), i * 2);
  }
  return out;
}

/** Linear resampler for mono PCM16. */
export function resample(pcm: Buffer, from: number, to: number): Buffer {
  if (from === to) return pcm;
  const n = Math.floor(pcm.length / 2);
  const outN = Math.floor((n * to) / from);
  const out = Buffer.alloc(outN * 2);
  for (let i = 0; i < outN; i++) {
    const pos = (i * from) / to;
    const i0 = Math.floor(pos);
    const i1 = Math.min(i0 + 1, n - 1);
    const f = pos - i0;
    const v = pcm.readInt16LE(i0 * 2) * (1 - f) + pcm.readInt16LE(i1 * 2) * f;
    out.writeInt16LE(Math.round(v), i * 2);
  }
  return out;
}

/** Root-mean-square level, 0..1. */
export function rms(pcm: Buffer): number {
  const n = Math.floor(pcm.length / 2);
  if (n === 0) return 0;
  let s = 0;
  for (let i = 0; i < n; i++) { const v = pcm.readInt16LE(i * 2) / 32768; s += v * v; }
  return Math.sqrt(s / n);
}

/** Largest absolute PCM16 sample value. */
export function peakAbs(pcm: Buffer): number {
  const n = Math.floor(pcm.length / 2);
  let peak = 0;
  for (let i = 0; i < n; i++) { const v = Math.abs(pcm.readInt16LE(i * 2)); if (v > peak) peak = v; }
  return peak;
}

/** PCM16 to Float32 in [-1, 1], with gain and clipping. */
export function pcm16ToFloat32(pcm: Buffer, gain = 1): Float32Array {
  const n = Math.floor(pcm.length / 2);
  const out = new Float32Array(n);
  for (let i = 0; i < n; i++) out[i] = Math.max(-1, Math.min(1, (pcm.readInt16LE(i * 2) / 32768) * gain));
  return out;
}

/** Float32 in [-1, 1] to PCM16 (clamped, scaled by 32767). */
export function float32ToPcm16(samples: ArrayLike<number>): Buffer {
  const out = Buffer.alloc(samples.length * 2);
  for (let i = 0; i < samples.length; i++) {
    out.writeInt16LE(Math.round(Math.max(-1, Math.min(1, samples[i])) * 32767), i * 2);
  }
  return out;
}
