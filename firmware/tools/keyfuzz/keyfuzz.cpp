// Drives the real App the way main.cpp does on the device and sends random keys, link and Wi-Fi changes.
// Built with AddressSanitizer and UBSan: any memory error stops the run with a stack trace.
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include "core/app.h"
#include "core/settings.h"
#include "core/store.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/updater.h"
#include "screens/screens.h"

// main.cpp reads millis() once per loop; handlers that call nowMs() run later (I2C, crypto).
static uint32_t g_now = 1000, g_late = 0;
uint32_t nowMs() { return g_now + g_late; }
static std::mt19937 rng(1);
static int pick(int n) { return (int)(rng() % (uint32_t)n); }

class FuzzStore : public SettingsStore {
 public:
  std::map<std::string, std::string> kv;
  std::string load(const char* k) override { return kv[k]; }
  void save(const char* k, const std::string& v) override { kv[k] = v; }
  void wipe() override { kv.clear(); }
};

class FuzzLink : public LinkApi {
 public:
  LinkState st = LinkState::Offline;
  std::string err, code = "482913", target = "host";
  HostInfo h;
  FuzzLink() { h.id = "9f2c1a"; h.name = "host"; h.os = "darwin"; }
  LinkState state() const override { return st; }
  const std::string& error() const override { return err; }
  const std::string& pairCode() const override { return code; }
  const HostInfo& host() const override { return h; }
  const std::string& targetName() const override { return target; }
  void connectHost(const HostEntry&) override { st = LinkState::Connecting; }
  void connectNew(const std::string&, uint16_t, const std::string&) override { st = LinkState::Connecting; }
  void disconnect() override { st = LinkState::Offline; }
  uint32_t call(const char* m, JsonDocument&, RpcCallback cb, uint32_t) override {
    JsonDocument r;
    bool ok = st == LinkState::Ready && pick(4);
    if (ok) deserializeJson(r, reply(m));
    pending.push_back({ok, r, ok ? "" : "offline"});
    pendingCb.push_back(cb);
    return (uint32_t)pending.size();
  }
  bool sendMic(const int16_t*, size_t) override { return st == LinkState::Ready; }
  uint32_t rttMs() const override { return 23; }

  void flush() {
    auto cbs = std::move(pendingCb);
    auto res = std::move(pending);
    pendingCb.clear();
    pending.clear();
    for (size_t i = 0; i < cbs.size(); i++) cbs[i](res[i].ok, res[i].doc.as<JsonVariantConst>(), res[i].ok ? nullptr : res[i].err.c_str());
  }

 private:
  struct Res { bool ok; JsonDocument doc; std::string err; };
  std::vector<Res> pending;
  std::vector<RpcCallback> pendingCb;
  static const char* reply(const char* m) {
    std::string s = m;
    if (s == "agent.tail") return R"({"lines":["one","two","Allow? [y/n]"]})";
    if (s == "procs.list") return R"({"procs":[{"pid":1,"name":"node","cpu":8,"mem":1,"user":"dev"}]})";
    if (s == "term.open") return R"({"tid":1})";
    if (s == "agent.create") return R"({"id":"d-9"})";
    if (s == "harness.list") return R"({"harness":[{"id":"direct:pi","name":"Pi","backend":"direct","ok":true,"color":1}]})";
    if (s == "projects.list") return R"({"projects":[{"path":"/x","name":"x","branch":"main"}]})";
    if (s == "agent.last") return R"({"text":"hello there, this is the reply"})";
    if (s == "ota.read") return R"({"n":8192})";
    return "{}";
  }
};

class FuzzNet : public NetApi {
 public:
  WifiState st = WifiState::Off;
  bool scan_ = false;
  uint32_t scanEnd = 0;
  std::vector<WifiScanItem> results;
  std::vector<Beacon> bs;
  WifiState wifiState() const override { return st; }
  std::string ssid() const override { return st == WifiState::Off ? "" : "HomeNet"; }
  std::string ip() const override { return st == WifiState::Connected ? "192.168.1.2" : ""; }
  int rssi() const override { return -50; }
  std::string mac() const override { return "00:11:22:33:44:55"; }
  void connectWifi(const std::string&, const std::string&) override { st = WifiState::Connecting; }
  void connectSaved() override { if (!settings().wifi.empty()) st = WifiState::Connecting; }
  void disconnectWifi() override { st = WifiState::Off; }
  void startScan() override { if (!scan_) { scan_ = true; scanEnd = g_now + 1500; } }
  bool scanning() const override { return scan_; }
  const std::vector<WifiScanItem>& scanResults() const override { return results; }
  void startDiscovery() override {}
  bool discovering() const override { return false; }
  const std::vector<Beacon>& beacons() const override { return bs; }
  void loop() {
    if (scan_ && g_now > scanEnd) {
      scan_ = false;
      results.clear();
      int n = pick(4);
      for (int i = 0; i < n; i++) results.push_back({"net" + std::to_string(pick(6)), -40 - pick(50), (bool)pick(2)});
    }
  }
};

class FuzzAudio : public AudioApi {
 public:
  bool mic = false;
  void setVolume(uint8_t) override {}
  void click() override {}
  void chime(Chime) override {}
  bool micStart(uint8_t) override { return mic = true; }
  void micStop() override { mic = false; }
  bool micActive() const override { return mic; }
  float micLevel() const override { return 0.5f; }
  void micPoll(const std::function<void(const int16_t*, size_t)>& sink) override {
    static int16_t buf[800];
    if (mic) sink(buf, 800);
  }
  void speakerBegin(uint32_t) override {}
  void speakerFeed(const uint8_t*, size_t) override {}
  void speakerEnd() override {}
  void speakerStop() override {}
  bool speaking() const override { return false; }
};

class FuzzSys : public SystemApi {
 public:
  int batteryPct() const override { return 46; }
  bool charging() const override { return false; }
  float batteryVolts() const override { return 3.7f; }
  void setBrightness(uint8_t) override {}
  void setRotationFlipped(bool) override {}
  void setLed(uint16_t, uint8_t) override {}
  void setCpuMhz(uint16_t) override {}
  uint32_t freeHeap() const override { return 180000; }
  uint32_t minFreeHeap() const override { return 140000; }
  std::string chipInfo() const override { return "ESP32-S3"; }
  std::string deviceId() const override { return "c12a7e04b9d3"; }
  uint32_t uptimeS() const override { return g_now / 1000; }
  bool localTime(int& h, int& m, int& s) const override { h = 1; m = 2; s = 3; return true; }
  void setTimezone(const char*) override {}
  void setTimeFromHost(int64_t) override {}
  void reboot() override {}
  void factoryReset() override { settings() = Settings(); }
  void powerOff() override {}
};

class FuzzOta : public OtaApi {
 public:
  std::string err = "fuzz error";
  bool underLauncher() const override { return launcher; }
  bool sdReady() override { return pick(3); }
  std::vector<SdImage> sdImages() override {
    std::vector<SdImage> v;
    for (int i = pick(4); i > 0; i--) v.push_back({"/thronectl/fw" + std::to_string(i) + ".bin", "fw" + std::to_string(i) + ".bin", (uint32_t)pick(2000000)});
    return v;
  }
  bool begin(uint32_t, const std::string&, const std::string&) override { return pick(10); }
  bool write(const uint8_t*, size_t) override { return pick(100); }
  bool finish() override { return pick(4); }
  void abort() override {}
  const std::string& error() const override { return err; }
  uint32_t openSd(const std::string&) override { return pick(3) ? 1 + pick(60000) : 0; }
  int readSd(uint8_t* buf, size_t n) override {
    if (!pick(200)) return -1;
    memset(buf, 0xE9, n);
    return (int)n;
  }
  void closeSd() override {}
  bool launcher = false;
};

static FuzzStore store_;
static FuzzLink link_;
static FuzzNet net_;
static FuzzAudio audio_;
static FuzzSys sys_;
static FuzzOta ota_;

// A KIND_OTA frame body: the next offset the updater expects, or a random one.
static void otaFrame() {
  static uint8_t buf[4 + 8192];
  uint32_t at = pick(5) ? updater().written() : (uint32_t)rng();
  size_t n = 1 + pick(8192), left = updater().size() - updater().written();
  if (pick(4) && left && n > left) n = left;
  buf[0] = at >> 24; buf[1] = at >> 16; buf[2] = at >> 8; buf[3] = at;
  memset(buf + 4, 0xE9, n);
  app().onOtaData(buf, 4 + n);
}

static KeyEvent randomKey() {
  static const uint16_t special[] = {K_UP, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_BKSP, K_DEL, K_ESC, K_TAB, K_F1, K_F5, K_F12};
  KeyEvent e;
  int r = pick(10);
  if (r < 3) e.key = special[pick(sizeof special / sizeof special[0])];
  else if (r < 5) e.key = (uint16_t)";.,/`"[pick(5)];
  else e.key = (uint16_t)(0x20 + pick(0x5f));
  e.shift = pick(6) == 0;
  e.ctrl = pick(12) == 0;
  e.alt = pick(16) == 0;
  e.opt = pick(10) == 0;
  e.fn = pick(10) == 0;
  return e;
}

static void linkEvent(const char* ev, const char* json) {
  JsonDocument d;
  deserializeJson(d, json);
  app().onLinkEvent(ev, d.as<JsonVariantConst>());
}

static void randomWorld() {
  switch (pick(15)) {
    case 0: link_.st = (LinkState)pick(6); break;
    case 1: net_.st = (WifiState)pick(4); break;
    case 2: linkEvent("stats", R"({"cpu":37,"cores":[30,45],"load":[1,2,3],"mem":[2000,8000],"disk":[10,100],"net":[1,2],"up":5})"); break;
    case 3: linkEvent("agents", R"({"agents":[{"id":"d-1","name":"a","harness":"direct:pi","backend":"direct","status":"waiting","cwd":"/x","age":3,"last":"Allow?"},{"id":"d-2","name":"b","harness":"direct:pi","backend":"direct","status":"working","cwd":"/y","age":9,"last":"..."}]})"); break;
    case 4: linkEvent("agents", R"({"agents":[]})"); break;
    case 5: linkEvent("notify", R"({"kind":"attn","title":"a","body":"Allow?","id":"d-1"})"); break;
    case 6: linkEvent("term", R"({"tid":1,"full":true,"cx":2,"cy":1,"cv":true,"rows":[[0,[[-1,-1,0,"$ ls ╭─╮ ⣿"]]],[30,[[-1,-1,0,"out of range row"]]]]})"); break;
    case 7: linkEvent("term_exit", R"({"tid":1,"code":0})"); break;
    case 8: link_.flush(); break;
    case 9: linkEvent("ota.offer", pick(2) ? R"({"ver":"9.9.9","size":3000,"sha256":"ab","from":"host"})" : R"({"size":0})"); break;
    case 10: case 11: otaFrame(); break;
    case 12: ota_.launcher = pick(2); break;
    default: break;
  }
}

int main(int argc, char** argv) {
  long steps = argc > 1 ? atol(argv[1]) : 200000;
  rng.seed(argc > 2 ? atoi(argv[2]) : 1);
  Canvas c;
  c.setColorDepth(16);
  c.createSprite(SCREEN_W, SCREEN_H);
  settingsBind(&store_);
  settingsLoad();
  svc().link = &link_;
  svc().net = &net_;
  svc().audio = &audio_;
  svc().sys = &sys_;
  svc().ota = &ota_;
  app().begin();
  uint32_t lastFrame = 0;
  UpdatePhase lastPhase = UpdatePhase::Idle;
  long phases[4] = {0};
  for (long i = 0; i < steps; i++) {
    g_now += 1 + pick(40);
    uint32_t now = g_now;
    g_late = 1 + pick(30);
    bool pressed = pick(3) == 0;
    if (pressed) app().onKey(randomKey());
    if (pick(200) == 0) app().onPtt(true);
    if (pick(150) == 0) app().onPtt(false);
    if (pick(8) == 0) randomWorld();
    net_.loop();
    if (pick(4) == 0) link_.flush();
    app().loop(now);
    ui().tick(now);
    if (pressed && !app().screenOn()) {
      printf("keyfuzz: FAIL at step %ld: the screen turned off right after a key press\n", i);
      return 1;
    }
    if (app().screenOn() && elapsedMs(now, lastFrame) >= app().frameInterval(now)) {
      lastFrame = now;
      ui().render(c);
    }
    if (updater().phase() != lastPhase) phases[(int)(lastPhase = updater().phase())]++;
    g_late = 0;
  }
  printf("keyfuzz: %ld steps, no memory errors (updates: %ld started, %ld done, %ld failed)\n", steps, phases[1], phases[2], phases[3]);
  return 0;
}
