#include <M5Cardputer.h>
#include <Preferences.h>
#include <esp_system.h>
#include <sys/time.h>
#include <ctime>
#include "hal.h"

static const char* NS = "throne";

void SystemHal::begin() {
  battAt_ = 0;
  loop(millis());
}

void SystemHal::loop(uint32_t now) {
  if (battAt_ && now - battAt_ < 5000) return;
  battAt_ = now ? now : 1;
  int lvl = M5.Power.getBatteryLevel();
  batt_ = lvl >= 0 && lvl <= 100 ? lvl : -1;
  volts_ = M5.Power.getBatteryVoltage() / 1000.0f;
  chg_ = M5.Power.isCharging() == m5::Power_Class::is_charging;
}

void SystemHal::setBrightness(uint8_t b) {
  if (b == brightness_) return;
  brightness_ = b;
  M5.Display.setBrightness(b);
}

void SystemHal::setRotationFlipped(bool flipped) {
  int r = flipped ? 3 : 1;
  if (r == rotation_) return;
  rotation_ = r;
  M5.Display.setRotation(r);
}

void SystemHal::setLed(uint16_t c, uint8_t level) {
  if (!level) c = 0;
  if (c == ledColor_ && level == ledLevel_) return;
  ledColor_ = c;
  ledLevel_ = level;
  uint8_t r = ((c >> 11) & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  M5.Led.setBrightness(level);
  M5.Led.setAllColor(r, g, b);
}

void SystemHal::setCpuMhz(uint16_t mhz) { setCpuFrequencyMhz(mhz); }
uint32_t SystemHal::freeHeap() const { return ESP.getFreeHeap(); }
uint32_t SystemHal::minFreeHeap() const { return ESP.getMinFreeHeap(); }

std::string SystemHal::chipInfo() const {
  char b[64];
  snprintf(b, sizeof b, "%s rev%d, %u MHz, %u MB flash", ESP.getChipModel(), ESP.getChipRevision(), (unsigned)getCpuFrequencyMhz(),
           (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
  return b;
}

std::string SystemHal::deviceId() const {
  uint64_t mac = ESP.getEfuseMac();
  char b[20];
  snprintf(b, sizeof b, "%012llx", (unsigned long long)mac);
  return b;
}

uint32_t SystemHal::uptimeS() const { return millis() / 1000; }

bool SystemHal::localTime(int& h, int& m, int& s) const {
  time_t now = time(nullptr);
  if (now < 1700000000) return false;
  tm t;
  localtime_r(&now, &t);
  h = t.tm_hour;
  m = t.tm_min;
  s = t.tm_sec;
  return true;
}

void SystemHal::setTimezone(const char* posix) {
  setenv("TZ", posix && *posix ? posix : "UTC0", 1);
  tzset();
}

void SystemHal::setTimeFromHost(int64_t unixMs) {
  if (time(nullptr) > 1700000000 || unixMs < 1700000000000LL) return;
  timeval tv{(time_t)(unixMs / 1000), (suseconds_t)((unixMs % 1000) * 1000)};
  settimeofday(&tv, nullptr);
}

void SystemHal::startNtp() {
  const char* tz = getenv("TZ");
  std::string keep = tz ? tz : "UTC0";
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  setTimezone(keep.c_str());
}

void SystemHal::reboot() { ESP.restart(); }

void SystemHal::factoryReset() {
  // Only this firmware's namespace: a boot manager such as Launcher keeps its own NVS data.
  NvsStore().wipe();
  ESP.restart();
}

void SystemHal::powerOff() {
  M5.Display.setBrightness(0);
  M5.Led.setAllColor(0, 0, 0);
  M5.Power.powerOff();
}

std::string NvsStore::load(const char* key) {
  Preferences p;
  if (!p.begin(NS, true)) return "";
  size_t len = p.getBytesLength(key);
  std::string s(len, '\0');
  if (len) p.getBytes(key, &s[0], len);
  p.end();
  return s;
}

void NvsStore::save(const char* key, const std::string& value) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putBytes(key, value.data(), value.size());
  p.end();
}

void NvsStore::wipe() {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.clear();
  p.end();
}

bool NvsStore::loadBlob(const char* key, uint8_t* out, size_t len) {
  Preferences p;
  if (!p.begin(NS, true)) return false;
  bool ok = p.getBytesLength(key) == len && p.getBytes(key, out, len) == len;
  p.end();
  return ok;
}

void NvsStore::saveBlob(const char* key, const uint8_t* data, size_t len) {
  Preferences p;
  if (!p.begin(NS, false)) return;
  p.putBytes(key, data, len);
  p.end();
}
