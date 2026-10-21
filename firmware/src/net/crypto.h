#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

/** Primitives for the thronectl handshake and channel. Backed by mbedTLS. */
namespace tcrypto {

void random(uint8_t* out, size_t len);
/** Fills priv with a clamped random scalar and pub with its X25519 public key. */
bool keypair(uint8_t priv[32], uint8_t pub[32]);
/** Derives the public key for an existing private scalar. */
bool publicKey(const uint8_t priv[32], uint8_t pub[32]);
bool x25519(const uint8_t priv[32], const uint8_t peerPub[32], uint8_t out[32]);

class Sha256 {
 public:
  Sha256();
  ~Sha256();
  void update(const void* data, size_t len);
  void update(const char* s);
  void finish(uint8_t out[32]);

 private:
  void* ctx_;
};

void sha256(const void* data, size_t len, uint8_t out[32]);
void hmacSha256(const uint8_t* key, size_t keyLen, const uint8_t* data, size_t len, uint8_t out[32]);
void hkdfSha256(const uint8_t* salt, size_t saltLen, const uint8_t* ikm, size_t ikmLen, const char* info, uint8_t* out, size_t outLen);

/** AES-256-GCM with the protocol nonce: 4 zero bytes, then the 64-bit big-endian counter. */
bool seal(const uint8_t key[32], uint64_t counter, const uint8_t* plain, size_t len, uint8_t* out);
bool open(const uint8_t key[32], uint64_t counter, const uint8_t* sealed, size_t len, uint8_t* out);

std::string b64encode(const uint8_t* data, size_t len);
bool b64decode(const char* s, uint8_t* out, size_t len);
std::string hex(const uint8_t* data, size_t len);

struct SessionKeys {
  uint8_t dh[32], hd[32], th[32];
  char sas[7];
};

/** Key schedule from docs/protocol.md. ss and ee are the two X25519 outputs. */
void deriveKeys(const uint8_t dpk[32], const uint8_t depk[32], const uint8_t dn[16], const uint8_t hpk[32],
                const uint8_t hepk[32], const uint8_t hn[16], const uint8_t ss[32], const uint8_t ee[32], SessionKeys& k);
/** Host or device ID: hex of the first 8 bytes of SHA-256(pub). */
std::string keyId(const uint8_t pub[32]);

}  // namespace tcrypto
