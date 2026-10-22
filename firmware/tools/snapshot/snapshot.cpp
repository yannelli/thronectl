// Host-side renderer: drives the real UI code with mock services and writes one PNG per scenario.
#include <cmath>
#include <cstdio>
#include <map>
#include "core/app.h"
#include "core/settings.h"
#include "core/store.h"
#include "core/theme.h"
#include "core/ui.h"
#include "core/updater.h"
#include "screens/screens.h"

static uint32_t g_now = 100000;
uint32_t nowMs() { return g_now; }

class MockLink : public LinkApi {
 public:
  LinkState st = LinkState::Ready;
  std::string err, code = "482913", target = "Studio MacBook";
  HostInfo h;
  std::map<std::string, std::string> replies;
  bool hold = false;  // keep callbacks instead of answering, like a slow host
  std::vector<RpcCallback> held;
  MockLink() {
    h.id = "9f2c1a"; h.name = "Studio MacBook"; h.hostname = "studio"; h.os = "darwin"; h.arch = "arm64";
    h.user = "dev"; h.ver = "0.1.0"; h.paseo = true;
  }
  LinkState state() const override { return st; }
  const std::string& error() const override { return err; }
  const std::string& pairCode() const override { return code; }
  const HostInfo& host() const override { return h; }
  const std::string& targetName() const override { return target; }
  void connectHost(const HostEntry&) override {}
  void connectNew(const std::string&, uint16_t, const std::string&) override {}
  void disconnect() override {}
  uint32_t call(const char* m, JsonDocument&, RpcCallback cb, uint32_t) override {
    if (hold) {
      held.push_back(cb);
      return 1;
    }
    auto it = replies.find(m);
    JsonDocument r;
    deserializeJson(r, it == replies.end() ? "{}" : it->second);
    cb(true, r.as<JsonVariantConst>(), nullptr);
    return 1;
  }
  bool sendMic(const int16_t*, size_t) override { return true; }
  uint32_t rttMs() const override { return 23; }
  bool bt = false;
  bool bluetooth() const override { return bt; }
};

class MockBle : public BleApi {
 public:
  bool on = false;
  std::string err;
  bool begin() override { return on = true; }
  void end() override { on = false; }
  bool started() const override { return on; }
  void advertise(bool, bool) override {}
  LinkPipe* accepted() override { return nullptr; }
  bool hostConnected() const override { return false; }
  const std::string& error() const override { return err; }
};

class MockNet : public NetApi {
 public:
  std::vector<WifiScanItem> scan{{"HomeNet-5G", -48, true}, {"Coffee Shop", -71, false}, {"xfinitywifi", -80, false}, {"NETGEAR42", -86, true}};
  std::vector<Beacon> bs{{"77ab01", "build-box", "192.168.1.40", "linux", 4787}};
  WifiState wifiState() const override { return WifiState::Connected; }
  std::string ssid() const override { return "HomeNet-5G"; }
  std::string ip() const override { return "192.168.1.23"; }
  int rssi() const override { return -52; }
  std::string mac() const override { return "DC:54:75:C1:2A:7E"; }
  void connectWifi(const std::string&, const std::string&) override {}
  void connectSaved() override {}
  void disconnectWifi() override {}
  void startScan() override {}
  bool scanning() const override { return false; }
  const std::vector<WifiScanItem>& scanResults() const override { return scan; }
  void startDiscovery() override {}
  bool discovering() const override { return false; }
  const std::vector<Beacon>& beacons() const override { return bs; }
};

class MockAudio : public AudioApi {
 public:
  void setVolume(uint8_t) override {}
  void click() override {}
  void chime(Chime) override {}
  bool micStart(uint8_t) override { return true; }
  void micStop() override {}
  bool micActive() const override { return true; }
  float micLevel() const override { return 0.7f; }
  void micPoll(const std::function<void(const int16_t*, size_t)>& sink) override {
    static int16_t buf[1600];
    sink(buf, 1600);
  }
  void speakerBegin(uint32_t) override {}
  void speakerFeed(const uint8_t*, size_t) override {}
  void speakerEnd() override {}
  void speakerStop() override {}
  bool speaking() const override { return false; }
};

class MockSys : public SystemApi {
 public:
  int batteryPct() const override { return 76; }
  bool charging() const override { return false; }
  float batteryVolts() const override { return 3.98f; }
  void setBrightness(uint8_t) override {}
  void setRotationFlipped(bool) override {}
  void setLed(Rgb) override {}
  void setCpuMhz(uint16_t) override {}
  uint32_t freeHeap() const override { return 182000; }
  uint32_t minFreeHeap() const override { return 141000; }
  std::string chipInfo() const override { return "ESP32-S3 rev0.2, 8 MB flash"; }
  std::string deviceId() const override { return "c12a7e04b9d3e811"; }
  uint32_t uptimeS() const override { return 5025; }
  bool localTime(int& h, int& m, int& s) const override { h = 14; m = 7; s = 0; return true; }
  void setTimezone(const char*) override {}
  void setTimeFromHost(int64_t) override {}
  void reboot() override {}
  void factoryReset() override {}
  std::string storageInfo() const override { return "flash 8 of 1536 KB"; }
  void powerOff() override {}
};

class MockOta : public OtaApi {
 public:
  bool launcher = false, finishOk = true;
  std::string err;
  bool underLauncher() const override { return launcher; }
  bool sdReady() override { return !noFat; }
  std::vector<SdImage> sdImages() override {
    return {{"/thronectl-cardputer-adv-v0.2.0.bin", "thronectl-cardputer-adv-v0.2.0.bin", 1248480},
            {"/thronectl/thronectl-cardputer-adv-v0.1.2.bin", "thronectl-cardputer-adv-v0.1.2.bin", 1247904}};
  }
  bool begin(uint32_t, const std::string&, const std::string&) override { return true; }
  bool write(const uint8_t*, size_t) override { return true; }
  bool finish() override { return finishOk; }
  void abort() override {}
  const std::string& error() const override { return err; }
  uint32_t openSd(const std::string&) override { return 1182944; }
  int readSd(uint8_t*, size_t n) override { return (int)n; }
  void closeSd() override {}
  bool noFat = false;
  bool sdList(const std::string& dir, std::vector<SdEntry>& out) override {
    if (dir == "/") out = {{"backups", true}, {"thronectl", true}, {"music", true}, {"notes.json", false, 2100}};
    else out = {{"old", true}, {"thronectl-settings.json", false, 3872}, {"settings.json", false, 3410}};
    return true;
  }
  bool sdNeedsFormat() const override { return noFat; }
};

static MockLink link_;
static MockBle ble_;
static MockOta ota_;
static MockNet net_;
static MockAudio audio_;
static MockSys sys_;
static Canvas* canvas;
static const char* outDir = ".";
static const char* suffix = "";

static void shot(const char* name) {
  for (int i = 0; i < 3; i++) {
    ui().tick(g_now);
    g_now += 16;
  }
  ui().render(*canvas);
  size_t len = 0;
  void* png = canvas->createPng(&len, 0, 0, SCREEN_W, SCREEN_H);
  std::string path = std::string(outDir) + "/" + name + suffix + ".png";
  FILE* f = fopen(path.c_str(), "wb");
  if (f && png) {
    fwrite(png, 1, len, f);
    fclose(f);
  }
  free(png);
  printf("%s\n", path.c_str());
}

static KeyEvent key(uint16_t k, bool shift = false) {
  KeyEvent e;
  e.key = k;
  e.shift = shift;
  return e;
}

static void type(const char* s) {
  for (; *s; s++) ui().handleKey(key((uint8_t)*s));
}

// Feeds the updater `n` chunks the way the host's KIND_OTA frames would.
static void otaChunks(int n) {
  static uint8_t buf[4 + Updater::CHUNK];
  for (int i = 0; i < n && updater().active(); i++) {
    uint32_t at = updater().written(), len = updater().size() - at < Updater::CHUNK ? updater().size() - at : Updater::CHUNK;
    buf[0] = at >> 24; buf[1] = at >> 16; buf[2] = at >> 8; buf[3] = at;
    buf[4] = 0xE9;
    app().onOtaData(buf, 4 + len);
  }
}

static void seed() {
  JsonDocument d;
  deserializeJson(d, R"J({"cpu":37.5,"cores":[30,45,20,60],"load":[3.1,2.4,2.0],"mem":[22118,32768],"swap":[512,2048],
    "disk":[612.4,994.7],"net":[1840,220],"up":372000,"procs":611,"batt":{"pct":81,"chg":true},
    "agents":{"working":2,"idle":2,"waiting":1}})J");
  for (int i = 0; i < 64; i++) {
    d["cpu"] = 30 + 25 * sin(i / 5.0) + (i % 7) * 3;
    d["mem"][0] = 20000 + i * 30;
    store().applyStats(d);
  }
  deserializeJson(d, R"J({"agents":[
    {"id":"a1b2c3","name":"fix login redirect","harness":"paseo:claude","backend":"paseo","status":"waiting","cwd":"/Users/dev/code/webapp","age":340,"last":"Wants to run: npm test"},
    {"id":"d-1","name":"refactor billing","harness":"direct:codex","backend":"direct","status":"working","cwd":"/Users/dev/code/api","age":1260,"last":"Editing src/billing/invoice.ts"},
    {"id":"d-2","name":"write docs","harness":"direct:pi","backend":"direct","status":"working","cwd":"/Users/dev/code/docs","age":95,"last":"Reading README.md"},
    {"id":"e5f6a7","name":"upgrade deps","harness":"paseo:cursor","backend":"paseo","status":"idle","cwd":"/Users/dev/code/mobile","age":7200,"last":"Done. 14 packages upgraded."},
    {"id":"d-3","name":"flaky test hunt","harness":"direct:antigravity","backend":"direct","status":"exited","cwd":"/Users/dev/code/api","age":86400,"last":"exit 0"}]})J");
  store().applyAgents(d);
  deserializeJson(d, R"J({"harness":[
    {"id":"paseo:claude","name":"Claude Code","backend":"paseo","ok":true,"color":55018,"mode":"default","modes":[{"id":"plan","label":"Plan"},{"id":"default","label":"Always ask"},{"id":"acceptEdits","label":"Accept edits"},{"id":"bypassPermissions","label":"Bypass"}]},
    {"id":"paseo:codex","name":"Codex","backend":"paseo","ok":true,"color":2335,"mode":"auto","modes":[{"id":"auto","label":"Default"},{"id":"full-access","label":"Full access"}]},
    {"id":"direct:pi","name":"Pi","backend":"direct","ok":true,"color":51037},
    {"id":"direct:cursor","name":"Cursor Agent","backend":"direct","ok":true,"color":32543},
    {"id":"direct:antigravity","name":"Antigravity","backend":"direct","ok":true,"color":17023},
    {"id":"direct:gemini","name":"Gemini CLI","backend":"direct","ok":false,"color":36351}]})J");
  store().applyHarness(d);
  deserializeJson(d, R"J({"projects":[{"path":"/Users/dev/code/webapp","name":"webapp","branch":"main"},
    {"path":"/Users/dev/code/api","name":"api","branch":"feat/billing-v2"},{"path":"/Users/dev/code/mobile","name":"mobile","branch":"develop"},
    {"path":"/Users/dev/code/docs","name":"docs","branch":"main"},{"path":"/Users/dev","name":"~","branch":""}]})J");
  store().applyProjects(d);
  store().addNotice({"done", "refactor billing", "Finished: 6 files changed, tests pass", "d-1", g_now - 60000});
  store().addNotice({"attn", "fix login redirect", "Wants to run: npm test", "a1b2c3", g_now - 20000});
  link_.replies["agent.tail"] = R"J({"lines":["> fix the login redirect loop on /account","","I'll start by finding where the redirect happens.","","Read src/auth/session.ts (142 lines)","Read src/routes/account.tsx (88 lines)","","The loop comes from useSession() returning null while loading,","so the guard redirects before the cookie is read. I'll add a","loading state to the guard.","","Edit src/routes/account.tsx  +6 -2","","Bash(npm test -- auth)","  Allow this command? [y/n]"]})J";
  link_.replies["agent.pending"] = R"J({"tool":"Bash","text":"npm test -- auth\n\nRuns the auth test suite in src/auth. Claude wants to check that the redirect guard waits for the session to load before it sends the user to /login."})J";
  link_.replies["procs.list"] = R"J({"procs":[{"pid":4127,"name":"node","cpu":88.2,"mem":3.1,"user":"dev"},{"pid":812,"name":"WindowServer","cpu":21.4,"mem":1.2,"user":"_windowserver"},
    {"pid":33021,"name":"claude","cpu":12.9,"mem":2.4,"user":"dev"},{"pid":3301,"name":"Google Chrome Helper (Renderer)","cpu":9.5,"mem":4.8,"user":"dev"},
    {"pid":2210,"name":"codex","cpu":6.1,"mem":1.0,"user":"dev"},{"pid":101,"name":"kernel_task","cpu":4.2,"mem":0.6,"user":"root"},
    {"pid":5512,"name":"thronectl","cpu":1.3,"mem":0.4,"user":"dev"},{"pid":901,"name":"Spotlight","cpu":0.9,"mem":0.7,"user":"dev"},{"pid":77,"name":"launchd","cpu":0.1,"mem":0.1,"user":"root"}]})J";
  link_.replies["term.open"] = R"J({"tid":1})J";
  link_.replies["config.groups"] = R"J({"groups":[{"id":"general","label":"General","sub":"name, shell, stats"},{"id":"network","label":"Network","sub":"port, bind, discovery"},
    {"id":"projects","label":"Projects","sub":"folders to list"},{"id":"paseo","label":"Paseo","sub":"backend"},{"id":"voice","label":"Voice","sub":"speech providers"},
    {"id":"harnesses","label":"Harnesses","sub":"agent CLIs"},{"id":"keys","label":"Keys","sub":"API keys and env"}]})J";
  link_.replies["config.fields"] = R"J({"fields":[{"key":"env.OPENAI_API_KEY","label":"OPENAI_API_KEY","type":"secret","value":"sk-...9fQa"},
    {"key":"env.GROQ_API_KEY","label":"GROQ_API_KEY","type":"secret","value":""},{"key":"env.ELEVENLABS_API_KEY","label":"ELEVENLABS_API_KEY","type":"secret","value":""},
    {"key":"env.ANTHROPIC_API_KEY","label":"ANTHROPIC_API_KEY","type":"secret","value":"sk-...k2Lw (from the daemon's environment)"},
    {"key":"env.GEMINI_API_KEY","label":"GEMINI_API_KEY","type":"secret","value":""},{"key":"env","label":"Add a variable","type":"add","value":""}]})J";
  Settings& s = settings();
  HostEntry h;
  h.id = "9f2c1a"; h.name = "Studio MacBook"; h.addr = "192.168.1.12"; h.os = "darwin";
  s.upsertHost(h);
  s.defaultHost = h.id;
  s.rememberWifi("HomeNet-5G", "x");
}

static void termFrame() {
  JsonDocument d;
  deserializeJson(d, R"J({"tid":1,"full":true,"cx":4,"cy":11,"cv":true,"rows":[
    [0,[[-1,-1,0,"╭──────────────────────────────────────╮"]]],
    [1,[[-1,-1,0,"│ "],[64512,-1,1,"✻ Welcome to Claude Code!"],[-1,-1,0,"            │"]]],
    [2,[[-1,-1,0,"│   "],[33808,-1,0,"cwd: ~/code/webapp"],[-1,-1,0,"                 │"]]],
    [3,[[-1,-1,0,"╰──────────────────────────────────────╯"]]],
    [5,[[-1,-1,0,"> "],[-1,-1,0,"fix the login redirect loop"]]],
    [7,[[2016,-1,0,"⏺ "],[-1,-1,0,"Update(src/routes/account.tsx)"]]],
    [8,[[33808,-1,0,"  ⎿  Updated with 6 additions"]]],
    [9,[[2016,-1,0,"   + "],[-1,2016,0,"if (loading) return <Spinner/>"]]],
    [10,[[63488,-1,0,"   - "],[-1,63488,0,"if (!user) redirect('/login')"]]],
    [11,[[-1,-1,0,"❯ ▌"],[33808,-1,0,"  ░▒▓█ ⣿⡇ ▁▃▅▇"]]],
    [13,[[33808,-1,8," ? for shortcuts "],[33808,-1,0,"          ✓ tests pass"]]]]})J");
  if (app().termSink) app().termSink("term", d.as<JsonVariantConst>());
}

static void shots() {
  seed();
  Settings& s = settings();
  applyTheme(0, 0);
  ota_.launcher = false;
  ota_.finishOk = true;
  ota_.err.clear();
  updater().reset();
  ui().home();
  g_now += 20000;
  ui().push(makeHome());
  shot("home");
  for (int i = 1; i < 6; i++) {
    ui().handleKey(key('/'));
    std::string n = "home_slide" + std::to_string(i);
    shot(n.c_str());
  }
  ui().handleKey(key('/'));
  ui().push(makeSplash());
  g_now += 800;
  shot("splash");
  ui().home();
  openLauncher();
  shot("launcher");
  ui().home();
  ui().push(makeAgents());
  shot("agents");
  ui().handleKey(key('.'));
  shot("agents_sel2");
  ui().home();
  ui().push(makeAgent("a1b2c3"));
  shot("agent_detail");
  ui().home();
  ui().push(makeNewAgent(""));
  shot("new_1_harness");
  ui().handleKey(key(K_ENTER));
  shot("new_2_project");
  ui().handleKey(key(K_ENTER));
  shot("new_3_mode");
  ui().handleKey(key(K_ENTER));
  type("add a dark mode toggle to settings");
  shot("new_4_prompt");
  ui().home();
  ui().push(makeTerminal("d-1", "refactor billing"));
  termFrame();
  shot("terminal");
  s.termFont = 0;
  ui().home();
  ui().push(makeTerminal("d-1", "refactor billing"));
  termFrame();
  shot("terminal_small");
  s.termFont = 1;
  ui().home();
  ui().push(makeProcs());
  shot("procs");
  ui().home();
  ui().push(makeHosts());
  shot("hosts");
  link_.st = LinkState::Pairing;
  ui().push(makePairing());
  shot("pairing");
  link_.st = LinkState::Ready;
  ui().home();
  ui().push(makeWifi());
  shot("wifi");
  ui().home();
  ui().push(makeSettings());
  shot("settings");
  ui().handleKey(key(K_ENTER));
  shot("settings_display");
  ui().home();
  ui().push(makeSettings());
  ui().handleKey(key('.'));
  ui().handleKey(key('.'));
  ui().handleKey(key(K_ENTER));
  shot("settings_voice");
  ui().home();
  ui().push(makeSettingsCategory(1, 0));
  shot("settings_sound");
  ui().home();
  ui().push(makeSettingsCategory(7, 0));
  shot("settings_power");
  ui().home();
  ui().push(makeApprovals());
  shot("approvals");
  settings().simpleMode = true;
  ui().home();
  ui().top()->update(nowMs());
  shot("simple_home");
  settings().simpleMode = false;
  ui().home();
  ui().top()->update(nowMs());
  ui().push(makeSettingsCategory(3, 4));
  shot("settings_led");
  ui().handleKey(key(K_ENTER));
  shot("settings_led_standby");
  LedSlot savedLed = s.leds[LED_STANDBY];
  s.leds[LED_STANDBY].fx = led::FX_COUNT - 6;
  s.leds[LED_STANDBY].color = led::COLOR_COUNT - 2;
  shot("settings_led_new");
  ui().handleKey(key(K_ENTER));
  shot("settings_led_effects");
  ui().handleKey(key('`'));
  ui().handleKey(key('.'));
  ui().handleKey(key(K_ENTER));
  shot("settings_led_colors");
  s.leds[LED_STANDBY] = savedLed;
  ui().home();
  svc().ble = &ble_;
  settings().bluetooth = true;
  ble_.on = link_.bt = true;
  ui().push(makeSettingsCategory(5, 0));
  shot("settings_bluetooth");
  settings().bluetooth = false;
  ble_.on = link_.bt = false;
  svc().ble = nullptr;
  ui().home();
  ui().push(makeHostConfig("", "Host settings"));
  shot("host_settings");
  ui().home();
  ui().push(makeHostConfig("keys", "Keys"));
  shot("host_keys");
  ui().home();
  std::string keys = link_.replies["config.fields"];
  link_.replies["config.fields"] = R"J({"fields":[{"key":"voice.localOn","label":"Use local voice","type":"bool","value":true},
    {"key":"voice.install","label":"Installing","type":"action","value":"kokoro 42%","live":true},
    {"key":"voice.stt.provider","label":"Speech to text","type":"choice","value":"auto","options":["auto","local","openai"]},
    {"key":"voice.stt.model","label":"STT model","type":"text","value":""},{"key":"voice.tts.provider","label":"Text to speech","type":"choice","value":"auto","options":["auto","local"]},
    {"key":"voice.tts.voice","label":"TTS voice","type":"text","value":"af_sky"}]})J";
  ui().push(makeHostConfig("voice", "Host speech"));
  shot("host_voice");
  link_.replies["config.fields"] = keys;
  ui().home();
  ui().push(makeSdBrowser(true));
  shot("sd_backup");
  ui().home();
  ui().push(makeSdBrowser(false));
  shot("sd_restore");
  ui().home();
  ota_.noFat = true;
  ota_.err = "The card has no FAT32 volume (exFAT does not work)";
  ui().push(makeSdBrowser(true));
  shot("sd_nofat");
  ota_.noFat = false;
  ota_.err.clear();
  ui().home();
  ui().home();
  ui().push(makeKeyHelp());
  shot("keyhelp");
  ui().home();
  ui().push(makeNotices());
  shot("notices");
  ui().home();
  ui().push(makeAbout());
  shot("about");
  ui().home();
  ui().confirm("End agent?", "refactor billing", [](bool) {});
  shot("confirm");
  ui().home();
  ui().toast("done", "refactor billing", "Finished: 6 files changed, tests pass");
  shot("toast");
  ui().home();
  app().voice.pttDown();
  g_now += 2300;
  shot("voice_rec");
  app().voice.cancel();
  link_.hold = true;
  g_now += 20000;  // let the toast from the earlier shot expire
  ui().home();
  {
    JsonDocument d;
    deserializeJson(d, R"({"ver":"0.2.0","size":1182944,"sha256":"","from":"Studio MacBook"})");
    app().onLinkEvent("ota.offer", d.as<JsonVariantConst>());
  }
  shot("update_offer");
  ui().handleKey(key('y'));
  otaChunks(60);
  shot("update_progress");
  updater().cancel("cancelled on the device");
  ota_.launcher = true;
  updater().startLink("0.2.0", 16384, "", true);
  otaChunks(2);
  ui().home();
  ui().push(makeUpdate());
  shot("update_saved_sd");
  ota_.launcher = false;
  ota_.finishOk = false;
  ota_.err = "Checksum mismatch: the image was damaged on the way";
  updater().startLink("0.2.0", 16384, "", false);
  otaChunks(2);
  shot("update_failed");
  ui().home();
  ui().push(makeSdUpdate());
  shot("update_sd_list");
  link_.hold = false;
  link_.held.clear();
  link_.st = LinkState::Offline;
  ui().home();
  shot("home_offline");
  link_.st = LinkState::Ready;
  const int themes[] = {2, 4, 8, 10, 11, 12, 13, 14};
  for (int t : themes) {
    applyTheme(t, 0);
    ui().home();
    std::string n = std::string("theme_") + std::to_string(t);
    shot(n.c_str());
  }
  applyTheme(0, 0);
}

int main(int argc, char** argv) {
  if (argc > 1) outDir = argv[1];
  Canvas c;
  c.setColorDepth(16);
  c.createSprite(SCREEN_W, SCREEN_H);
  canvas = &c;
  svc().link = &link_;
  svc().net = &net_;
  svc().audio = &audio_;
  svc().sys = &sys_;
  svc().ota = &ota_;
  settings().splash = false;
  const char* const SUFFIX[] = {"", "_m", "_l"};
  for (int size = 0; size < 3; size++) {
    applyTextSize(size);
    suffix = SUFFIX[size];
    shots();
  }
  return 0;
}
