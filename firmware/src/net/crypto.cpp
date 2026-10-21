#include "crypto.h"
#include <cstdio>
#include <cstring>
#include <mbedtls/base64.h>
#include <mbedtls/ecp.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#ifdef ARDUINO
#include <esp_random.h>
#else
#include <cstdio>
#endif

namespace tcrypto {

void random(uint8_t* out, size_t len) {
#ifdef ARDUINO
  esp_fill_random(out, len);
#else
  FILE* f = fopen("/dev/urandom", "rb");
  size_t n = f ? fread(out, 1, len, f) : 0;
  if (f) fclose(f);
  if (n != len) memset(out, 0, len);
#endif
}

static int rng(void*, unsigned char* out, size_t len) {
  random(out, len);
  return 0;
}

static bool scalarMul(const uint8_t scalar[32], const uint8_t* point, uint8_t out[32]) {
  mbedtls_ecp_group grp;
  mbedtls_ecp_point P, R;
  mbedtls_mpi d;
  mbedtls_ecp_group_init(&grp);
  mbedtls_ecp_point_init(&P);
  mbedtls_ecp_point_init(&R);
  mbedtls_mpi_init(&d);
  uint8_t k[32];
  memcpy(k, scalar, 32);
  k[0] &= 248;
  k[31] &= 127;
  k[31] |= 64;
  int ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
  if (!ret) ret = mbedtls_mpi_read_binary_le(&d, k, 32);
  if (!ret) {
    if (point) {
      uint8_t u[32];
      memcpy(u, point, 32);
      u[31] &= 127;
      ret = mbedtls_mpi_read_binary_le(&P.X, u, 32);
      if (!ret) ret = mbedtls_mpi_lset(&P.Z, 1);
    } else {
      ret = mbedtls_ecp_copy(&P, &grp.G);
    }
  }
  if (!ret) ret = mbedtls_ecp_mul(&grp, &R, &d, &P, rng, nullptr);
  if (!ret) ret = mbedtls_mpi_write_binary_le(&R.X, out, 32);
  mbedtls_mpi_free(&d);
  mbedtls_ecp_point_free(&R);
  mbedtls_ecp_point_free(&P);
  mbedtls_ecp_group_free(&grp);
  memset(k, 0, sizeof k);
  return ret == 0;
}

bool keypair(uint8_t priv[32], uint8_t pub[32]) {
  random(priv, 32);
  priv[0] &= 248;
  priv[31] &= 127;
  priv[31] |= 64;
  return scalarMul(priv, nullptr, pub);
}

bool publicKey(const uint8_t priv[32], uint8_t pub[32]) { return scalarMul(priv, nullptr, pub); }

bool x25519(const uint8_t priv[32], const uint8_t peerPub[32], uint8_t out[32]) {
  if (!scalarMul(priv, peerPub, out)) return false;
  uint8_t acc = 0;
  for (int i = 0; i < 32; i++) acc |= out[i];
  return acc != 0;
}

static const mbedtls_md_info_t* sha() { return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256); }

Sha256::Sha256() {
  auto* c = new mbedtls_md_context_t;
  mbedtls_md_init(c);
  mbedtls_md_setup(c, sha(), 0);
  mbedtls_md_starts(c);
  ctx_ = c;
}

Sha256::~Sha256() {
  auto* c = (mbedtls_md_context_t*)ctx_;
  mbedtls_md_free(c);
  delete c;
}

void Sha256::update(const void* data, size_t len) { mbedtls_md_update((mbedtls_md_context_t*)ctx_, (const unsigned char*)data, len); }
void Sha256::update(const char* s) { update(s, strlen(s)); }
void Sha256::finish(uint8_t out[32]) { mbedtls_md_finish((mbedtls_md_context_t*)ctx_, out); }

void sha256(const void* data, size_t len, uint8_t out[32]) { mbedtls_md(sha(), (const unsigned char*)data, len, out); }

void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len, uint8_t out[32]) {
  mbedtls_md_hmac(sha(), key, keyLen, data, len, out);
}

void hkdfSha256(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen, const char* info, uint8_t* out, size_t outLen) {
  uint8_t prk[32], t[32];
  hmacSha256(salt, saltLen, ikm, ikmLen, prk);
  size_t infoLen = strlen(info), done = 0, tLen = 0;
  uint8_t buf[32 + 64 + 1];
  for (uint8_t i = 1; done < outLen; i++) {
    size_t n = 0;
    memcpy(buf, t, tLen);
    n += tLen;
    memcpy(buf + n, info, infoLen);
    n += infoLen;
    buf[n++] = i;
    hmacSha256(prk, 32, buf, n, t);
    tLen = 32;
    size_t take = outLen - done < 32 ? outLen - done : 32;
    memcpy(out + done, t, take);
    done += take;
  }
  memset(prk, 0, sizeof prk);
}

static void nonce(uint64_t counter, uint8_t iv[12]) {
  memset(iv, 0, 4);
  for (int i = 0; i < 8; i++) iv[4 + i] = (uint8_t)(counter >> (56 - 8 * i));
}

bool seal(const uint8_t key[32], uint64_t counter, const uint8_t* plain, size_t len, uint8_t* out) {
  uint8_t iv[12];
  nonce(counter, iv);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int ret = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (!ret) ret = mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, len, iv, 12, nullptr, 0, plain, out, 16, out + len);
  mbedtls_gcm_free(&g);
  return ret == 0;
}

bool open(const uint8_t key[32], uint64_t counter, const uint8_t* sealed, size_t len, uint8_t* out) {
  if (len < 16) return false;
  uint8_t iv[12];
  nonce(counter, iv);
  mbedtls_gcm_context g;
  mbedtls_gcm_init(&g);
  int ret = mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  if (!ret) ret = mbedtls_gcm_auth_decrypt(&g, len - 16, iv, 12, nullptr, 0, sealed + len - 16, 16, sealed, out);
  mbedtls_gcm_free(&g);
  return ret == 0;
}

std::string b64encode(const uint8_t* data, size_t len) {
  std::string out(4 * ((len + 2) / 3) + 1, '\0');
  size_t olen = 0;
  mbedtls_base64_encode((unsigned char*)&out[0], out.size(), &olen, data, len);
  out.resize(olen);
  return out;
}

bool b64decode(const char* s, uint8_t* out, size_t len) {
  if (!s) return false;
  uint8_t buf[96];
  size_t olen = 0;
  if (mbedtls_base64_decode(buf, sizeof buf, &olen, (const unsigned char*)s, strlen(s)) || olen != len) return false;
  memcpy(out, buf, len);
  return true;
}

std::string hex(const uint8_t* data, size_t len) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < len; i++) {
    s += d[data[i] >> 4];
    s += d[data[i] & 15];
  }
  return s;
}

void deriveKeys(const uint8_t dpk[32], const uint8_t depk[32], const uint8_t dn[16], const uint8_t hpk[32],
                const uint8_t hepk[32], const uint8_t hn[16], const uint8_t ss[32], const uint8_t ee[32], SessionKeys& k) {
  {
    Sha256 h;
    h.update("thronectl-v1");
    h.update(dpk, 32);
    h.update(depk, 32);
    h.update(dn, 16);
    h.update(hpk, 32);
    h.update(hepk, 32);
    h.update(hn, 16);
    h.finish(k.th);
  }
  uint8_t ikm[64], okm[64];
  memcpy(ikm, ss, 32);
  memcpy(ikm + 32, ee, 32);
  hkdfSha256(k.th, 32, ikm, 64, "thronectl keys", okm, 64);
  memcpy(k.dh, okm, 32);
  memcpy(k.hd, okm + 32, 32);
  uint8_t s[32];
  Sha256 h;
  h.update("thronectl-sas");
  h.update(k.th, 32);
  h.finish(s);
  uint32_t n = ((uint32_t)s[0] << 24 | (uint32_t)s[1] << 16 | (uint32_t)s[2] << 8 | s[3]) % 1000000;
  snprintf(k.sas, sizeof k.sas, "%06u", (unsigned)n);
  memset(ikm, 0, sizeof ikm);
  memset(okm, 0, sizeof okm);
}

std::string keyId(const uint8_t pub[32]) {
  uint8_t h[32];
  sha256(pub, 32, h);
  return hex(h, 8);
}

}  // namespace tcrypto
