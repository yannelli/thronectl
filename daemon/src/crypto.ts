import {
  createCipheriv, createDecipheriv, createHash, createPrivateKey, createPublicKey,
  diffieHellman, generateKeyPairSync, hkdfSync, KeyObject, randomBytes,
} from "node:crypto";

export interface KeyPair {
  priv: Buffer;
  pub: Buffer;
}

const b64u = (b: Buffer): string => b.toString("base64url");

export function generateKeyPair(): KeyPair {
  const { privateKey } = generateKeyPairSync("x25519");
  const jwk = privateKey.export({ format: "jwk" });
  return { priv: Buffer.from(jwk.d!, "base64url"), pub: Buffer.from(jwk.x!, "base64url") };
}

function privKeyObject(kp: KeyPair): KeyObject {
  return createPrivateKey({ key: { kty: "OKP", crv: "X25519", d: b64u(kp.priv), x: b64u(kp.pub) }, format: "jwk" });
}

function pubKeyObject(pub: Buffer): KeyObject {
  return createPublicKey({ key: { kty: "OKP", crv: "X25519", x: b64u(pub) }, format: "jwk" });
}

export function x25519(self: KeyPair, peerPub: Buffer): Buffer {
  if (peerPub.length !== 32) throw new Error("bad public key length");
  return diffieHellman({ privateKey: privKeyObject(self), publicKey: pubKeyObject(peerPub) });
}

export function sha256(...parts: (Buffer | string)[]): Buffer {
  const h = createHash("sha256");
  for (const p of parts) h.update(p);
  return h.digest();
}

export function keyId(pub: Buffer): string {
  return sha256(pub).subarray(0, 8).toString("hex");
}

export interface SessionKeys {
  dh: Buffer; // device to host
  hd: Buffer; // host to device
  sas: string;
  th: Buffer;
}

export interface HandshakeInputs {
  dpk: Buffer; depk: Buffer; dn: Buffer;
  hpk: Buffer; hepk: Buffer; hn: Buffer;
  ss: Buffer; // static-static DH
  ee: Buffer; // ephemeral-ephemeral DH
}

export function deriveKeys(i: HandshakeInputs): SessionKeys {
  const th = sha256("thronectl-v1", i.dpk, i.depk, i.dn, i.hpk, i.hepk, i.hn);
  const okm = Buffer.from(hkdfSync("sha256", Buffer.concat([i.ss, i.ee]), th, "thronectl keys", 64));
  const n = sha256("thronectl-sas", th).readUInt32BE(0) % 1_000_000;
  return { dh: okm.subarray(0, 32), hd: okm.subarray(32, 64), sas: n.toString().padStart(6, "0"), th };
}

function nonce(counter: bigint): Buffer {
  const n = Buffer.alloc(12);
  n.writeBigUInt64BE(counter, 4);
  return n;
}

export function seal(key: Buffer, counter: bigint, plain: Buffer): Buffer {
  const c = createCipheriv("aes-256-gcm", key, nonce(counter));
  const body = Buffer.concat([c.update(plain), c.final()]);
  return Buffer.concat([body, c.getAuthTag()]);
}

export function open(key: Buffer, counter: bigint, sealed: Buffer): Buffer {
  if (sealed.length < 16) throw new Error("frame too short");
  const d = createDecipheriv("aes-256-gcm", key, nonce(counter));
  d.setAuthTag(sealed.subarray(sealed.length - 16));
  return Buffer.concat([d.update(sealed.subarray(0, sealed.length - 16)), d.final()]);
}

export const random = (n: number): Buffer => randomBytes(n);
