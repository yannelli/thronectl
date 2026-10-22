// Runs the firmware's Link and crypto code on the host against a real thronectl daemon.
#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "core/settings.h"
#include "core/theme.h"
#include "core/updater.h"
#include "net/crypto.h"
#include "net/link.h"

// Settings pull in the LED defaults, which read theme colors. No screens run here.
const Theme& T() {
  static const Theme t{"none"};
  return t;
}

uint32_t nowMs() {
  using namespace std::chrono;
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static Services g_svc;
Services& svc() { return g_svc; }

// Collects the image in memory; finish() checks the SHA-256 the host offered.
class CheckOta : public OtaApi {
 public:
  std::vector<uint8_t> got;
  std::string sha, err;
  bool underLauncher() const override { return false; }
  bool sdReady() override { return false; }
  std::vector<SdImage> sdImages() override { return {}; }
  bool begin(uint32_t size, const std::string& s, const std::string&) override {
    got.clear();
    got.reserve(size);
    sha = s;
    return true;
  }
  bool write(const uint8_t* d, size_t n) override {
    got.insert(got.end(), d, d + n);
    return true;
  }
  bool finish() override {
    uint8_t h[32];
    tcrypto::sha256(got.data(), got.size(), h);
    if (toHex(h, 32) == sha) return true;
    err = "sha256 mismatch";
    return false;
  }
  void abort() override {}
  const std::string& error() const override { return err; }
  uint32_t openSd(const std::string&) override { return 0; }
  int readSd(uint8_t*, size_t) override { return -1; }
  void closeSd() override {}
};

static Link link_;
static CheckOta ota_;
static int offers = 0;
static int failures = 0;
static std::string termText;
static int eventsSeen = 0;

#define CHECK(cond, msg)                         \
  do {                                           \
    if (cond) printf("ok   %s\n", msg);          \
    else { printf("FAIL %s\n", msg); failures++; } \
  } while (0)

static void pump(uint32_t ms, const std::function<bool()>& until = nullptr) {
  uint32_t end = nowMs() + ms;
  while ((int32_t)(end - nowMs()) > 0) {
    link_.loop(nowMs());
    if (until && until()) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
}

struct Result {
  bool done = false, ok = false;
  std::string err, json;
};

static Result rpc(const char* method, const char* params, uint32_t waitMs = 8000) {
  JsonDocument p;
  deserializeJson(p, params);
  Result res;
  link_.call(method, p, [&res](bool ok, JsonVariantConst r, const char* err) {
    res.done = true;
    res.ok = ok;
    res.err = err ? err : "";
    serializeJson(r, res.json);
  });
  pump(waitMs, [&] { return res.done; });
  return res;
}

static void testRfcVector() {
  // RFC 7748 section 6.1
  uint8_t a[32], bpub[32], out[32];
  fromHex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
  fromHex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", bpub, 32);
  uint8_t apub[32];
  tcrypto::publicKey(a, apub);
  CHECK(toHex(apub, 32) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", "x25519 public key (RFC 7748)");
  tcrypto::x25519(a, bpub, out);
  CHECK(toHex(out, 32) == "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", "x25519 shared secret (RFC 7748)");
  uint8_t key[32] = {0}, sealed[64], plain[48];
  const char* msg = "hello throne";
  tcrypto::seal(key, 7, (const uint8_t*)msg, 12, sealed);
  CHECK(tcrypto::open(key, 7, sealed, 28, plain) && memcmp(plain, msg, 12) == 0, "aes-gcm round trip");
  sealed[3] ^= 1;
  CHECK(!tcrypto::open(key, 7, sealed, 28, plain), "aes-gcm rejects tampering");
}

// run.sh pushes argv[3] with `ota.push` once the ready file exists, like `thronectl flash`.
static void testOta(const char* image) {
  FILE* f = fopen(image, "rb");
  std::vector<uint8_t> file;
  if (f) {
    uint8_t b[4096];
    for (size_t n; (n = fread(b, 1, sizeof b, f)) > 0;) file.insert(file.end(), b, b + n);
    fclose(f);
  }
  CHECK(file.size() > 0x10000, "test image readable");
  if (file.size() <= 0x10000) return;
  if (const char* ready = getenv("LINKCHECK_OTA_READY")) fclose(fopen(ready, "w"));
  pump(10000, [] { return offers > 0; });
  CHECK(offers == 1, "host offers firmware (ota.offer)");
  uint32_t t0 = nowMs();
  pump(20000, [] { return !updater().active(); });
  CHECK(updater().phase() == UpdatePhase::Done, ("firmware transfer completes: " + updater().error()).c_str());
  std::vector<uint8_t> app(file.begin() + 0x10000, file.end());
  CHECK(ota_.got == app, "device received exactly the app part of the merged image");
  printf("     %u bytes in %u ms\n", (unsigned)ota_.got.size(), (unsigned)(nowMs() - t0));
  pump(500);
}

/** A LinkPipe over TCP that takes at most one BLE-sized packet per write, like the Bluetooth path. */
class SocketPipe : public LinkPipe {
 public:
  bool open(const char* host, uint16_t port) {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    inet_pton(AF_INET, host, &sa.sin_addr);
    if (connect(fd_, (sockaddr*)&sa, sizeof sa) != 0) return false;
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
    return true;
  }
  bool isOpen() const override { return fd_ >= 0; }
  size_t write(const uint8_t* d, size_t n) override {
    if (fd_ < 0) return 0;
    ssize_t k = send(fd_, d, n < 244 ? n : 244, MSG_DONTWAIT);
    return k > 0 ? (size_t)k : 0;
  }
  size_t read(uint8_t* d, size_t n) override {
    if (fd_ < 0) return 0;
    ssize_t k = recv(fd_, d, n, MSG_DONTWAIT);
    if (k == 0 || (k < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) close();
    return k > 0 ? (size_t)k : 0;
  }
  void close() override {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

 private:
  int fd_ = -1;
};

static void testPipe(const char* host, uint16_t port) {
  static SocketPipe pipe;
  CHECK(pipe.open(host, port), "pipe connects");
  link_.attach(&pipe, false);
  pump(10000, [] { return link_.state() == LinkState::Ready || link_.state() == LinkState::Failed || link_.state() == LinkState::Pairing; });
  CHECK(link_.state() == LinkState::Ready, ("known host over a Bluetooth-style pipe: " + link_.error()).c_str());
  CHECK(link_.bluetooth(), "link reports the pipe");
  Result r = rpc("harness.list", "{}");
  CHECK(r.ok && r.json.find("direct:echo") != std::string::npos, "harness.list over the pipe");
  r = rpc("config.groups", "{}");
  CHECK(r.ok && r.json.find("harnesses") != std::string::npos, "config.groups over the pipe");
  std::string addr = settings().hosts[0].addr;
  link_.disconnect();
  CHECK(!pipe.isOpen(), "disconnect closes the pipe");
  CHECK(settings().hosts[0].addr == addr, "the pipe keeps the saved Wi-Fi address");

  auto saved = settings().hosts;
  settings().hosts.clear();
  CHECK(pipe.open(host, port), "pipe connects again");
  link_.attach(&pipe, false);
  pump(5000, [] { return link_.state() == LinkState::Failed || link_.state() == LinkState::Ready; });
  CHECK(link_.state() == LinkState::Failed && link_.error().find("unknown host") != std::string::npos, "an unknown host is refused outside pairing");
  settings().hosts = saved;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: linkcheck <host> <port> [firmware.bin]\n");
    return 2;
  }
  const char* host = argv[1];
  uint16_t port = (uint16_t)atoi(argv[2]);
  setvbuf(stdout, nullptr, _IONBF, 0);
  testRfcVector();

  uint8_t priv[32], pub[32];
  tcrypto::keypair(priv, pub);
  link_.begin(priv, pub);
  link_.deviceName = [] { return std::string("linkcheck"); };
  svc().link = &link_;
  svc().ota = &ota_;
  link_.onOta = [](const uint8_t* d, size_t n) { updater().onData(d, n); };
  link_.onEvent = [](const char* ev, JsonVariantConst d) {
    eventsSeen++;
    if (!strcmp(ev, "ota.offer")) {
      offers++;
      updater().startLink(d["ver"] | "", d["size"] | 0u, d["sha256"] | "", false);
    }
    if (!strcmp(ev, "term"))
      for (JsonArrayConst row : d["rows"].as<JsonArrayConst>())
        for (JsonArrayConst span : row[1].as<JsonArrayConst>()) termText += span[3].as<const char*>() ? span[3].as<const char*>() : "";
  };

  link_.connectNew(host, port, "test host");
  pump(10000, [] { return link_.state() == LinkState::Pairing || link_.state() == LinkState::Failed; });
  CHECK(link_.state() == LinkState::Pairing, "unknown device is asked to pair");
  printf("CODE %s\n", link_.pairCode().c_str());
  pump(20000, [] { return link_.state() == LinkState::Ready || link_.state() == LinkState::Failed; });
  CHECK(link_.state() == LinkState::Ready, ("paired and ready: " + link_.error()).c_str());
  if (link_.state() != LinkState::Ready) return 1;
  CHECK(settings().hosts.size() == 1 && settings().hosts[0].id == link_.host().id, "host saved after pairing");
  printf("     host %s (%s/%s) tzOff=%d\n", link_.host().name.c_str(), link_.host().os.c_str(), link_.host().arch.c_str(), link_.host().tzOffMin);

  Result r = rpc("ping", "{}");
  CHECK(r.ok && r.json.find("\"t\"") != std::string::npos, "ping");
  r = rpc("sub", R"({"topics":["stats","agents","notify"],"ms":1000})");
  CHECK(r.ok, "sub");
  r = rpc("stats.get", "{}");
  CHECK(r.ok && r.json.find("\"cpu\"") != std::string::npos, "stats.get");
  r = rpc("harness.list", "{}");
  CHECK(r.ok && r.json.find("direct:echo") != std::string::npos, "harness.list includes test harness");
  r = rpc("projects.list", "{}");
  CHECK(r.ok, "projects.list");
  r = rpc("procs.list", R"({"sort":"cpu","n":5})");
  CHECK(r.ok && r.json.find("\"pid\"") != std::string::npos, "procs.list");
  r = rpc("agent.create", R"({"harness":"direct:echo","cwd":"~","prompt":"hi from device","name":"probe"})");
  CHECK(r.ok, ("agent.create " + r.err).c_str());
  JsonDocument d;
  deserializeJson(d, r.json);
  std::string id = d["id"] | "";
  pump(1500);
  std::string p = R"({"id":")" + id + R"(","n":20})";
  r = rpc("agent.tail", p.c_str());
  CHECK(r.ok && r.json.find("READY") != std::string::npos, "agent.tail shows harness output");
  r = rpc("term.open", R"({"shell":true,"cwd":"~","cols":40,"rows":15})");
  CHECK(r.ok, ("term.open " + r.err).c_str());
  deserializeJson(d, r.json);
  int tid = d["tid"] | 0;
  pump(800);
  std::string in = R"({"tid":)" + std::to_string(tid) + R"(,"d":"echo th$((40+2))rone\r"})";
  rpc("term.input", in.c_str());
  pump(4000, [] { return termText.find("th42rone") != std::string::npos; });
  CHECK(termText.find("th42rone") != std::string::npos, "shell terminal streams screen updates");
  int16_t silence[1600] = {0};
  CHECK(link_.sendMic(silence, 1600), "mic frame accepted");
  pump(2500);
  CHECK(eventsSeen > 2, "events arrive (stats/agents/term)");
  std::string kill = R"({"id":")" + id + R"("})";
  CHECK(rpc("agent.kill", kill.c_str()).ok, "agent.kill");
  r = rpc("no.such.method", "{}");
  CHECK(!r.ok && !r.err.empty(), "unknown method returns an error");

  link_.disconnect();
  CHECK(link_.state() == LinkState::Offline, "disconnect");
  link_.connectHost(settings().hosts[0]);
  pump(10000, [] { return link_.state() == LinkState::Ready || link_.state() == LinkState::Failed || link_.state() == LinkState::Pairing; });
  CHECK(link_.state() == LinkState::Ready, "reconnect as a known device skips pairing");
  CHECK(rpc("ping", "{}").ok, "ping after reconnect");
  if (argc > 3) testOta(argv[3]);
  link_.disconnect();
  testPipe(host, port);
  printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
