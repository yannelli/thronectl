#include <M5Cardputer.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <sys/time.h>
#include <ctime>
#include "hal.h"

static const char* NS = "throne";
static constexpr uint8_t ES8311_ADDR = 0x18, BMI270_ADDR = 0x69;

void SystemHal::begin() {
  battAt_ = 0;
  loop(millis());
}

// Resting LiPo voltage to percent. M5Unified's straight line from 3.3 to 4.1 V reads high in the middle.
static int lipoPct(float v) {
  static const float V[] = {3.30f, 3.50f, 3.65f, 3.73f, 3.80f, 3.87f, 3.98f, 4.10f, 4.20f};
  static const int P[] = {0, 3, 10, 25, 40, 55, 75, 90, 100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (v < V[i]) return P[i - 1] + (int)((v - V[i - 1]) / (V[i] - V[i - 1]) * (P[i] - P[i - 1]));
  return 100;
}

void SystemHal::loop(uint32_t now) {
  if (battAt_ && now - battAt_ < 5000) return;
  battAt_ = now ? now : 1;
  int mv = 0;
  for (int i = 0; i < 8; i++) mv += M5.Power.getBatteryVoltage();
  float v = mv / 8000.0f;
  bool usb = HWCDC::isPlugged();
  if (v < 2.5f) {
    batt_ = -1;
    volts_ = 0;
    chg_ = usb;
    return;
  }
  volts_ = volts_ > 0 ? volts_ * 0.7f + v * 0.3f : v;
  batt_ = lipoPct(volts_);
  // The TP4057 status pins are not wired, so guess: a USB host is attached, or the voltage keeps rising.
  if (!trendAt_ || now - trendAt_ >= 60000) {
    rising_ = trendAt_ && volts_ - trendV_ > 0.015f;
    trendV_ = volts_;
    trendAt_ = now | 1;
  }
  chg_ = usb || rising_;
}

void SystemHal::setBrightness(uint8_t b) {
  led_.setBrightness(b);
}

void SystemHal::setPanelSleep(bool asleep) {
  if (asleep == asleep_) return;
  asleep_ = asleep;
  if (asleep) M5.Display.sleep();
  else {
    M5.Display.wakeup();
    delay(5);  // the ST7789 takes 5 ms after sleep out before it accepts pixels
  }
  led_.invalidate();
}

void SystemHal::setRotationFlipped(bool flipped) {
  int r = flipped ? 3 : 1;
  if (r == rotation_) return;
  rotation_ = r;
  M5.Display.setRotation(r);
}

void SystemHal::setLed(Rgb c) { led_.setColor(c); }
void SystemHal::setLedPower(bool powered) { led_.setPower(powered); }

void SystemHal::setCpuMhz(uint16_t mhz) {
  // 240, 160 and 80 MHz keep Wi-Fi running; the crystal speeds below 80 MHz stop it.
  if (mhz >= 80 && mhz != getCpuFrequencyMhz()) setCpuFrequencyMhz(mhz);
}
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
  setLed({});
  M5.Speaker.end();
  M5.Mic.end();
  // The rails stay up in deep sleep, so quiet the parts that would keep drawing current.
  M5.In_I2C.writeRegister8(ES8311_ADDR, 0x0D, 0xFC, 400000);
  M5.In_I2C.writeRegister8(ES8311_ADDR, 0x00, 0x00, 400000);
  M5.In_I2C.writeRegister8(BMI270_ADDR, 0x7D, 0x00, 400000);
  M5.In_I2C.writeRegister8(BMI270_ADDR, 0x7C, 0x03, 400000);
  M5.Display.sleep();
  // Hold the backlight switch off through sleep; setup() releases the hold.
  pinMode(GPIO_NUM_38, OUTPUT);
  digitalWrite(GPIO_NUM_38, LOW);
  gpio_hold_en(GPIO_NUM_38);
  gpio_deep_sleep_hold_en();
  while (digitalRead(GPIO_NUM_0) == LOW) delay(10);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  esp_deep_sleep_start();
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

bool NvsStore::bluetoothAtBoot() {
  NvsStore n;
  std::string cfg = n.load("cfg");
  return cfg.find("\"bluetooth\":true") != std::string::npos || cfg.find("\"blePairAtBoot\":true") != std::string::npos;
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
