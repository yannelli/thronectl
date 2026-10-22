#include "settings.h"
#include <ArduinoJson.h>

#define OPTS(name, ...)                                   \
  static const char* const name##_L[] = {__VA_ARGS__};    \
  const OptList name = {name##_L, (int)(sizeof(name##_L) / sizeof(name##_L[0]))}

OPTS(DIM_OPTS, "Never", "15 s", "30 s", "1 min", "2 min", "5 min");
OPTS(OFF_OPTS, "Never", "1 min", "2 min", "5 min", "10 min", "30 min");
OPTS(LED_OPTS, "Off", "Status and alerts", "Alerts only");
OPTS(LED_QUIET_OPTS, "Same", "Dim", "Off");
OPTS(LED_SCREEN_OFF_OPTS, "LED off", "Keep lit");
OPTS(CLICK_OPTS, "Off", "Soft", "Loud");
OPTS(NOTIFY_OPTS, "All", "Attention only", "Off");
OPTS(SPEAK_OPTS, "Off", "When done", "Done + attention");
OPTS(MIC_GAIN_OPTS, "Low", "Normal", "High", "Max");
OPTS(STATS_OPTS, "1 s", "2 s", "5 s");
OPTS(CPU_OPTS, "240 MHz", "160 MHz", "80 MHz");
OPTS(CPU_SAVE_OPTS, "Off", "When screen off", "When dimmed or off");
OPTS(WIFI_SAVE_OPTS, "Off", "Light", "Max", "Max when off");
OPTS(STATS_DIM_OPTS, "Same as active", "5 s", "10 s", "30 s");
OPTS(STATS_OFF_OPTS, "Same as dimmed", "30 s", "2 min", "Paused");
OPTS(DIM_LEVEL_OPTS, "Very low", "Low", "Medium", "High");
OPTS(QUIET_OPTS, "Off", "22:00 to 7:00", "23:00 to 8:00", "21:00 to 6:00", "20:00 to 9:00");
const uint8_t QUIET_FROM[] = {0, 22, 23, 21, 20}, QUIET_TO[] = {0, 7, 8, 6, 9};
OPTS(NOTIFY_DONE_OPTS, "Every turn", "Turns over 30 s", "Turns over 2 min", "Off");
const uint16_t NOTIFY_DONE_SECS[] = {0, 30, 120, 0};
OPTS(REMIND_OPTS, "Off", "Every 1 min", "Every 2 min", "Every 5 min");
const uint32_t REMIND_MS[] = {0, 60000, 120000, 300000};
OPTS(PTT_OPTS, "Hold to talk", "Tap to toggle");
OPTS(TERM_FONT_OPTS, "Small 60x20", "Normal 40x15");
OPTS(TEXT_SIZE_OPTS, "Small", "Medium", "Large");
OPTS(DASH_CYCLE_OPTS, "Off", "10 s", "30 s", "1 min");
const uint32_t DASH_CYCLE_MS[] = {0, 10000, 30000, 60000};
OPTS(REPEAT_DELAY_OPTS, "Short", "Medium", "Long");
OPTS(REPEAT_RATE_OPTS, "Fast", "Medium", "Slow");
OPTS(TZ_NAMES, "From host", "UTC", "US Eastern", "US Central", "US Mountain", "US Arizona", "US Pacific", "Alaska",
     "Hawaii", "UK / Ireland", "Central Europe", "Eastern Europe", "India", "China / Singapore", "Japan / Korea",
     "Sydney", "New Zealand", "Sao Paulo");

const char* const TZ_POSIX[] = {"", "UTC0", "EST5EDT,M3.2.0,M11.1.0", "CST6CDT,M3.2.0,M11.1.0",
                                "MST7MDT,M3.2.0,M11.1.0", "MST7", "PST8PDT,M3.2.0,M11.1.0",
                                "AKST9AKDT,M3.2.0,M11.1.0", "HST10", "GMT0BST,M3.5.0/1,M10.5.0",
                                "CET-1CEST,M3.5.0,M10.5.0/3", "EET-2EEST,M3.5.0/3,M10.5.0/4", "IST-5:30", "CST-8",
                                "JST-9", "AEST-10AEDT,M10.1.0,M4.1.0/3", "NZST-12NZDT,M9.5.0,M4.1.0/3", "<-03>3"};

const uint32_t DIM_MS[] = {0, 15000, 30000, 60000, 120000, 300000};
const uint32_t OFF_MS[] = {0, 60000, 120000, 300000, 600000, 1800000};
const uint32_t STATS_MS[] = {1000, 2000, 5000};
const uint32_t STATS_DIM_MS[] = {0, 5000, 10000, 30000};
const uint32_t STATS_OFF_MS[] = {0, 30000, 120000, 0};
const uint32_t REPEAT_DELAY_MS[] = {250, 400, 600};
const uint32_t REPEAT_RATE_MS[] = {35, 60, 100};
const uint8_t MIC_GAIN_VALUES[] = {8, 16, 28, 48};
const uint16_t CPU_VALUES[] = {240, 160, 80};

static Settings g_settings;
static SettingsStore* g_store = nullptr;

Settings& settings() { return g_settings; }
void settingsBind(SettingsStore* store) { g_store = store; }

std::string toHex(const uint8_t* data, size_t len) {
  static const char* d = "0123456789abcdef";
  std::string s;
  s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += d[data[i] >> 4];
    s += d[data[i] & 15];
  }
  return s;
}

bool fromHex(const std::string& hex, uint8_t* out, size_t len) {
  if (hex.size() != len * 2) return false;
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (size_t i = 0; i < len; i++) {
    int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (uint8_t)(hi << 4 | lo);
  }
  return true;
}

HostEntry* Settings::findHost(const std::string& id) {
  for (auto& h : hosts)
    if (h.id == id) return &h;
  return nullptr;
}

HostEntry* Settings::defaultHostEntry() {
  HostEntry* h = findHost(defaultHost);
  if (!h && !hosts.empty()) h = &hosts[0];
  return h;
}

void Settings::upsertHost(const HostEntry& h) {
  if (HostEntry* cur = findHost(h.id)) *cur = h;
  else {
    if (hosts.size() >= 6) hosts.erase(hosts.begin());
    hosts.push_back(h);
  }
  if (defaultHost.empty()) defaultHost = h.id;
}

void Settings::removeHost(const std::string& id) {
  for (size_t i = 0; i < hosts.size(); i++)
    if (hosts[i].id == id) {
      hosts.erase(hosts.begin() + i);
      break;
    }
  if (defaultHost == id) defaultHost = hosts.empty() ? "" : hosts[0].id;
}

void Settings::rememberWifi(const std::string& ssid, const std::string& pass) {
  WifiNet entry{ssid, pass};  // copy first: the arguments may point into `wifi`
  forgetWifi(entry.ssid);
  wifi.insert(wifi.begin(), entry);
  if (wifi.size() > 5) wifi.pop_back();
}

void Settings::forgetWifi(const std::string& ssid) {
  for (size_t i = 0; i < wifi.size(); i++)
    if (wifi[i].ssid == ssid) {
      wifi.erase(wifi.begin() + i);
      return;
    }
}

#define FIELDS(X)                                                                                    \
  X(brightness) X(theme) X(accent) X(dimAfter) X(offAfter) X(flip) X(termFont) X(animations)        \
  X(clock24) X(splash) X(showBattPct) X(ledMode) X(ledBright) X(volume) X(keyClick) X(notifySound) \
  X(startupChime) X(wakeOnNotify) X(toastSecs) X(speakReplies) X(micGain) X(pttMode) X(autoSend)   \
  X(tz) X(autoConnect) X(confirmKill) X(attachAfterCreate) X(showExited) X(statsRate) X(cpuMhz)    \
  X(repeatDelay) X(repeatRate) X(dashCycle) \
  X(cpuSave) X(wifiSave) X(statsDim) X(statsOff) X(dimLevel) X(lowBattSave) X(battAutoOff) X(alertVol)    \
  X(voiceVol) X(muted) X(dnd) X(quietHours) X(notifyDone) X(notifyAttn) X(notifyErr) X(remindWaiting) X(termAwake) X(ledQuiet) X(spaceTalk) X(simpleMode) X(ledScreenOff) X(bluetooth) X(blePairAtBoot)

#define STR_FIELDS(X) X(ttsVoice) X(deviceName) X(defaultHost) X(lastHarness) X(lastProject) X(lastMode) X(sdDir)

std::string settingsToJson(const Settings& s) {
  JsonDocument doc;
#define W(f) doc[#f] = s.f;
  FIELDS(W)
#undef W
#define WS(f) doc[#f] = s.f.c_str();
  STR_FIELDS(WS)
#undef WS
  doc["textScale"] = s.textSize;
  JsonArray wifi = doc["wifi"].to<JsonArray>();
  for (auto& w : s.wifi) {
    JsonObject o = wifi.add<JsonObject>();
    o["s"] = w.ssid.c_str();
    o["p"] = w.pass.c_str();
  }
  JsonArray hosts = doc["hosts"].to<JsonArray>();
  for (auto& h : s.hosts) {
    JsonObject o = hosts.add<JsonObject>();
    o["id"] = h.id.c_str();
    o["name"] = h.name.c_str();
    o["addr"] = h.addr.c_str();
    o["os"] = h.os.c_str();
    o["port"] = h.port;
    o["pk"] = toHex(h.pk, 32);
  }
  JsonArray led = doc["led"].to<JsonArray>();
  for (auto& l : s.leds) {
    JsonArray a = led.add<JsonArray>();
    for (uint8_t v : {l.fx, l.color, l.speed, l.level, l.length}) a.add(v);
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool settingsFromJson(Settings& s, const std::string& json) {
  JsonDocument doc;
  if (json.empty() || deserializeJson(doc, json)) return false;
#define R(f) \
  if (!doc[#f].isNull()) s.f = doc[#f].as<decltype(s.f)>();
  FIELDS(R)
#undef R
#define RS(f) \
  if (doc[#f].is<const char*>()) s.f = doc[#f].as<const char*>();
  STR_FIELDS(RS)
#undef RS
  // Before 0.4 "textSize" had two steps; its "Normal" is now "Large".
  if (!doc["textScale"].isNull()) s.textSize = doc["textScale"].as<uint8_t>();
  else if (!doc["textSize"].isNull()) s.textSize = doc["textSize"].as<uint8_t>() ? 2 : 0;
  s.wifi.clear();
  for (JsonObjectConst o : doc["wifi"].as<JsonArrayConst>()) s.wifi.push_back({o["s"] | "", o["p"] | ""});
  s.hosts.clear();
  for (JsonObjectConst o : doc["hosts"].as<JsonArrayConst>()) {
    HostEntry h;
    h.id = o["id"] | "";
    h.name = o["name"] | "";
    h.addr = o["addr"] | "";
    h.os = o["os"] | "";
    h.port = o["port"] | THRONE_PORT;
    if (!fromHex(o["pk"] | "", h.pk, 32) || h.id.empty()) continue;
    s.hosts.push_back(h);
  }
  JsonArrayConst led = doc["led"].as<JsonArrayConst>();
  for (size_t i = 0; i < led.size() && i < s.leds.size(); i++) {
    JsonArrayConst a = led[i].as<JsonArrayConst>();
    if (a.size() < 5) continue;
    s.leds[i] = {a[0].as<uint8_t>(), a[1].as<uint8_t>(), a[2].as<uint8_t>(), a[3].as<uint8_t>(), a[4].as<uint8_t>()};
  }
  return true;
}

void settingsLoad() {
  if (g_store) settingsFromJson(g_settings, g_store->load("cfg"));
}

void settingsSave() {
  if (g_store) g_store->save("cfg", settingsToJson(g_settings));
}
