#include "link.h"
#include <cerrno>
#include <fcntl.h>
#include "../core/settings.h"
#ifdef ARDUINO
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#else
// Host build for tools/linkcheck: same BSD socket API.
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
static uint32_t millis() { return nowMs(); }
#endif

static constexpr uint8_t KIND_JSON = 1, KIND_MIC = 2, KIND_SPK = 3, KIND_OTA = 4;
static constexpr size_t MAX_FRAME = 65536 + 17;
static constexpr size_t TX_LIMIT = 48 * 1024;

void Link::begin(const uint8_t priv[32], const uint8_t pub[32]) {
  memcpy(priv_, priv, 32);
  memcpy(pub_, pub, 32);
}

void Link::connectHost(const HostEntry& h) {
  target_ = h;
  haveTarget_ = true;
  targetName_ = h.name;
  start(h.addr, h.port);
}

void Link::connectNew(const std::string& addr, uint16_t port, const std::string& nameHint) {
  haveTarget_ = false;
  targetName_ = nameHint.empty() ? addr : nameHint;
  start(addr, port);
}

void Link::disconnect() {
  closeSocket();
  failPending("disconnected");
  st_ = LinkState::Offline;
  err_.clear();
}

void Link::closeSocket() {
  if (fd_ >= 0) close(fd_);
  fd_ = -1;
  rx_.clear();
  tx_.clear();
  sealed_ = false;
}

void Link::failPending(const char* why) {
  std::string w = why;
  for (auto& p : pending_) {
    RpcCallback cb = p.second.cb;
    deferred_.push_back([cb, w](bool, JsonVariantConst, const char*) { cb(false, JsonVariantConst(), w.c_str()); });
  }
  pending_.clear();
}

void Link::fail(const std::string& why) {
  closeSocket();
  failPending(why.c_str());
  err_ = why;
  st_ = LinkState::Failed;
}

void Link::start(const std::string& addr, uint16_t port) {
  closeSocket();
  failPending("reconnecting");
  addr_ = addr;
  port_ = port;
  err_.clear();
  code_.clear();
  host_ = HostInfo();
  txCtr_ = rxCtr_ = 0;
  stateAt_ = lastRx_ = millis();

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(port);
  if (inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(addr.c_str(), nullptr, &hints, &res) != 0 || !res) return fail("Cannot resolve " + addr);
    sa.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
  }
  fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ < 0) return fail("No socket available");
  fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
  int one = 1;
  setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  int r = ::connect(fd_, (sockaddr*)&sa, sizeof sa);
  if (r < 0 && errno != EINPROGRESS) return fail("Connection refused");
  st_ = LinkState::Connecting;
}

void Link::sendFrame(const uint8_t* data, size_t len) {
  char hdr[4] = {(char)(len >> 24), (char)(len >> 16), (char)(len >> 8), (char)len};
  tx_.append(hdr, 4);
  tx_.append((const char*)data, len);
}

void Link::sendJsonPlain(JsonDocument& doc) {
  std::string s;
  serializeJson(doc, s);
  sendFrame((const uint8_t*)s.data(), s.size());
}

void Link::sendSealed(uint8_t kind, const uint8_t* body, size_t len) {
  std::vector<uint8_t> plain(len + 1), out(len + 17);
  plain[0] = kind;
  memcpy(plain.data() + 1, body, len);
  if (!tcrypto::seal(keys_.dh, txCtr_++, plain.data(), plain.size(), out.data())) return fail("Encryption failed");
  sendFrame(out.data(), out.size());
}

void Link::sendHello() {
  tcrypto::keypair(epriv_, epub_);
  tcrypto::random(dn_, 16);
  uint8_t c[32];
  tcrypto::sha256(dn_, 16, c);
  JsonDocument d;
  d["t"] = "hello";
  d["v"] = 1;
  d["name"] = deviceName ? deviceName() : std::string("Cardputer");
  d["fw"] = FW_VERSION;
  d["pk"] = tcrypto::b64encode(pub_, 32);
  d["epk"] = tcrypto::b64encode(epub_, 32);
  d["c"] = tcrypto::b64encode(c, 32);
  sendJsonPlain(d);
}

bool Link::flush() {
  while (!tx_.empty()) {
    int n = send(fd_, tx_.data(), tx_.size(), MSG_DONTWAIT);
    if (n > 0) tx_.erase(0, n);
    else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
    else return false;
  }
  return true;
}

bool Link::receive() {
  uint8_t buf[1460];
  for (int i = 0; i < 16; i++) {
    int n = recv(fd_, buf, sizeof buf, MSG_DONTWAIT);
    if (n > 0) {
      rx_.insert(rx_.end(), buf, buf + n);
      lastRx_ = millis();
    } else if (n == 0) return false;
    else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
    else return false;
  }
  size_t off = 0;
  while (rx_.size() - off >= 4 && fd_ >= 0) {
    const uint8_t* p = rx_.data() + off;
    size_t len = (size_t)p[0] << 24 | (size_t)p[1] << 16 | (size_t)p[2] << 8 | p[3];
    if (len > MAX_FRAME) {
      fail("Frame too large");
      return true;
    }
    if (rx_.size() - off < 4 + len) break;
    std::vector<uint8_t> frame(p + 4, p + 4 + len);
    off += 4 + len;
    handleFrame(frame.data(), frame.size());
  }
  if (fd_ >= 0 && off) rx_.erase(rx_.begin(), rx_.begin() + off);
  return true;
}

void Link::handleFrame(const uint8_t* data, size_t len) {
  if (!sealed_) {
    JsonDocument doc;
    if (deserializeJson(doc, (const char*)data, len)) return fail("Bad handshake message");
    return handleHostHello(doc);
  }
  if (len < 17) return fail("Short frame");
  std::vector<uint8_t> plain(len - 16);
  if (!tcrypto::open(keys_.hd, rxCtr_++, data, len, plain.data())) return fail("Decryption failed");
  uint8_t kind = plain[0];
  if (kind == KIND_JSON) {
    JsonDocument doc;
    if (deserializeJson(doc, (const char*)plain.data() + 1, plain.size() - 1)) return;
    handleMessage(doc);
  } else if (kind == KIND_SPK && onSpeaker && st_ == LinkState::Ready) {
    onSpeaker(plain.data() + 1, plain.size() - 1);
  } else if (kind == KIND_OTA && onOta && st_ == LinkState::Ready) {
    onOta(plain.data() + 1, plain.size() - 1);
  }
}

void Link::handleHostHello(JsonDocument& doc) {
  if (st_ != LinkState::Handshake || strcmp(doc["t"] | "", "hello") != 0 || (doc["v"] | 0) != 1) return fail("Not a thronectl host");
  uint8_t hepk[32], hn[16];
  if (!tcrypto::b64decode(doc["pk"], hpk_, 32) || !tcrypto::b64decode(doc["epk"], hepk, 32) || !tcrypto::b64decode(doc["n"], hn, 16))
    return fail("Bad host hello");
  std::string id = tcrypto::keyId(hpk_);
  if (haveTarget_ && !target_.id.empty() && id != target_.id)
    return fail("Host key changed. Forget this host in Hosts, then pair again.");
  HostEntry* saved = settings().findHost(id);
  bool known = saved && memcmp(saved->pk, hpk_, 32) == 0;
  uint8_t ss[32], ee[32];
  if (!tcrypto::x25519(priv_, hpk_, ss) || !tcrypto::x25519(epriv_, hepk, ee)) return fail("Bad host key");
  tcrypto::deriveKeys(pub_, epub_, dn_, hpk_, hepk, hn, ss, ee, keys_);
  memset(ss, 0, sizeof ss);
  memset(ee, 0, sizeof ee);
  host_.id = id;
  host_.name = doc["name"] | "host";
  targetName_ = host_.name;
  JsonDocument r;
  r["t"] = "reveal";
  r["n"] = tcrypto::b64encode(dn_, 16);
  r["known"] = known;
  sendJsonPlain(r);
  sealed_ = true;
}

void Link::handleAuth(JsonDocument& doc) {
  const char* state = doc["state"] | "";
  if (!strcmp(state, "pair")) {
    if (strcmp(doc["code"] | "", keys_.sas) != 0) return fail("Pairing codes differ. Update the firmware or the daemon.");
    code_ = keys_.sas;
    st_ = LinkState::Pairing;
    stateAt_ = millis();
  } else if (!strcmp(state, "denied")) {
    fail("Pairing was declined on the host.");
  } else if (!strcmp(state, "ok")) {
    JsonObjectConst h = doc["host"];
    host_.name = h["name"] | host_.name.c_str();
    host_.hostname = h["hostname"] | "";
    host_.os = h["os"] | "";
    host_.arch = h["arch"] | "";
    host_.user = h["user"] | "";
    host_.ver = h["ver"] | "";
    host_.paseo = h["paseo"] | false;
    host_.hasTz = !h["tzOff"].isNull();
    host_.tzOffMin = h["tzOff"] | 0;
    if (!h["time"].isNull() && svc().sys) svc().sys->setTimeFromHost(h["time"].as<int64_t>());
    HostEntry e;
    e.id = host_.id;
    e.name = host_.name;
    e.addr = addr_;
    e.port = port_;
    e.os = host_.os;
    memcpy(e.pk, hpk_, 32);
    Settings& s = settings();
    HostEntry* cur = s.findHost(e.id);
    if (!cur || cur->addr != e.addr || cur->port != e.port || cur->name != e.name || cur->os != e.os) {
      s.upsertHost(e);
      settingsSave();
    }
    target_ = e;
    haveTarget_ = true;
    code_.clear();
    st_ = LinkState::Ready;
    lastPing_ = millis();
  }
}

void Link::handleMessage(JsonDocument& doc) {
  const char* t = doc["t"] | "";
  if (!strcmp(t, "auth")) return handleAuth(doc);
  if (st_ != LinkState::Ready) return;
  if (!strcmp(t, "res")) {
    uint32_t id = doc["id"] | 0u;
    auto it = pending_.find(id);
    if (it == pending_.end()) return;
    RpcCallback cb = it->second.cb;
    pending_.erase(it);
    bool ok = doc["ok"] | false;
    if (cb) cb(ok, doc["r"], ok ? nullptr : (doc["err"] | "error"));
  } else if (!strcmp(t, "ev") && onEvent) {
    onEvent(doc["e"] | "", doc["d"]);
  }
}

uint32_t Link::call(const char* method, JsonDocument& params, RpcCallback cb, uint32_t timeoutMs) {
  if (st_ != LinkState::Ready) {
    if (cb) deferred_.push_back([cb](bool, JsonVariantConst, const char*) { cb(false, JsonVariantConst(), "not connected"); });
    return 0;
  }
  uint32_t id = nextId_++;
  JsonDocument d;
  d["t"] = "req";
  d["id"] = id;
  d["m"] = method;
  d["p"] = params;
  std::string s;
  serializeJson(d, s);
  sendSealed(KIND_JSON, (const uint8_t*)s.data(), s.size());
  if (cb) pending_[id] = {cb, millis() + timeoutMs};
  return id;
}

bool Link::sendMic(const int16_t* pcm, size_t samples) {
  if (st_ != LinkState::Ready || tx_.size() > TX_LIMIT) return false;
  sendSealed(KIND_MIC, (const uint8_t*)pcm, samples * 2);
  return true;
}

void Link::loop(uint32_t now) {
  if (!deferred_.empty()) {
    auto list = std::move(deferred_);
    deferred_.clear();
    for (auto& cb : list) cb(false, JsonVariantConst(), nullptr);
  }
  if (fd_ < 0) return;
  if (st_ == LinkState::Connecting) {
    fd_set w;
    FD_ZERO(&w);
    FD_SET(fd_, &w);
    timeval tv{0, 0};
    if (select(fd_ + 1, nullptr, &w, nullptr, &tv) > 0) {
      int err = 0;
      socklen_t el = sizeof err;
      getsockopt(fd_, SOL_SOCKET, SO_ERROR, &err, &el);
      if (err) return fail(err == ECONNREFUSED ? "Connection refused. Is thronectl running?" : "Cannot reach host");
      st_ = LinkState::Handshake;
      stateAt_ = lastRx_ = now;
      sendHello();
    } else if (elapsedMs(now, stateAt_) > 6000) {
      return fail("Host did not answer");
    }
    return;
  }
  if (!flush()) return fail("Connection lost");
  if (!receive()) return fail(st_ == LinkState::Pairing ? "Pairing closed by the host" : "Connection closed by the host");
  if (fd_ < 0) return;
  if (!flush()) return fail("Connection lost");
  if (st_ == LinkState::Handshake && elapsedMs(now, stateAt_) > 15000) return fail("Handshake timed out");
  if (st_ == LinkState::Pairing && elapsedMs(now, stateAt_) > 130000) return fail("Pairing timed out");
  if (st_ == LinkState::Ready) {
    if (elapsedMs(now, lastPing_) > 10000) {
      lastPing_ = now;
      pingAt_ = now;
      call("ping", [this](bool ok, JsonVariantConst, const char*) { if (ok) rtt_ = millis() - pingAt_; });
    }
    if (elapsedMs(now, lastRx_) > 30000) return fail("Host stopped responding");
    for (auto it = pending_.begin(); it != pending_.end();) {
      if ((int32_t)(now - it->second.deadline) > 0) {
        RpcCallback cb = it->second.cb;
        it = pending_.erase(it);
        cb(false, JsonVariantConst(), "timed out");
      } else ++it;
    }
  }
}
