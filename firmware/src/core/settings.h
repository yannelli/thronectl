#pragma once
#include "platform.h"
#include "led.h"

struct WifiNet {
  std::string ssid, pass;
};

struct HostEntry {
  std::string id, name, addr, os;
  uint16_t port = THRONE_PORT;
  uint8_t pk[32] = {0};
};

struct Settings {
  // display
  uint8_t brightness = 150;
  uint8_t theme = 0, accent = 0;
  uint8_t dimAfter = 2;   // DIM_OPTS
  uint8_t offAfter = 3;   // OFF_OPTS
  bool flip = false;
  uint8_t textSize = 0;   // TEXT_SIZE_OPTS: 0 small, 1 medium, 2 large (saved as "textScale")
  uint8_t termFont = 1;   // 0 small (60x20), 1 normal (40x15)
  bool animations = true, clock24 = true, splash = true, showBattPct = true;
  uint8_t dashCycle = 0;  // DASH_CYCLE_OPTS
  bool termAwake = true;  // terminal output keeps the screen from dimming
  uint8_t ledMode = 1;    // LED_OPTS
  uint8_t ledBright = 64;
  uint8_t ledQuiet = 1;   // LED_QUIET_OPTS: in quiet hours and do not disturb
  uint8_t ledScreenOff = 0;  // LED_SCREEN_OFF_OPTS: the LED shares the backlight supply
  std::array<LedSlot, LED_SLOTS> leds = led::allDefaults();
  // sound
  uint8_t volume = 140;
  uint8_t keyClick = 1;   // CLICK_OPTS
  uint8_t notifySound = 0; // NOTIFY_OPTS
  bool startupChime = true, wakeOnNotify = true;
  uint8_t alertVol = 200, voiceVol = 255;  // channel levels under the master volume
  bool muted = false, dnd = false;
  uint8_t quietHours = 0;  // QUIET_OPTS
  uint8_t notifyDone = 0;  // NOTIFY_DONE_OPTS
  bool notifyAttn = true, notifyErr = true;
  uint8_t remindWaiting = 2;  // REMIND_OPTS
  uint8_t toastSecs = 4;
  // voice
  uint8_t speakReplies = 0; // SPEAK_OPTS
  std::string ttsVoice;
  uint8_t micGain = 1;    // MIC_GAIN_OPTS
  uint8_t pttMode = 0;    // 0 hold, 1 toggle
  bool autoSend = false;
  bool spaceTalk = true;  // space starts and stops talking on screens that do not type
  // network
  std::string deviceName;
  uint8_t tz = 0;         // TZ_NAMES / TZ_POSIX, 0 = from host
  std::vector<WifiNet> wifi;
  // hosts
  std::vector<HostEntry> hosts;
  std::string defaultHost;
  bool autoConnect = true;
  // device
  std::string sdDir = "/thronectl";  // last folder picked for the settings backup
  // agents
  std::string lastHarness, lastProject, lastMode;
  bool confirmKill = true, attachAfterCreate = false, showExited = true;
  bool simpleMode = false;  // home shows only the approval queue
  uint8_t statsRate = 1;  // STATS_OPTS
  // power and keyboard
  uint8_t cpuMhz = 0;     // CPU_OPTS
  uint8_t cpuSave = 1;    // CPU_SAVE_OPTS: drop to 80 MHz when dimmed or off
  uint8_t wifiSave = 1;   // WIFI_SAVE_OPTS
  uint8_t statsDim = 1, statsOff = 3;  // STATS_DIM_OPTS, STATS_OFF_OPTS
  uint8_t dimLevel = 1;   // DIM_LEVEL_OPTS
  bool lowBattSave = true, battAutoOff = true;
  uint8_t repeatDelay = 1, repeatRate = 1;

  HostEntry* findHost(const std::string& id);
  HostEntry* defaultHostEntry();
  void upsertHost(const HostEntry& h);
  void removeHost(const std::string& id);
  void rememberWifi(const std::string& ssid, const std::string& pass);
  void forgetWifi(const std::string& ssid);
};

struct OptList {
  const char* const* labels;
  int count;
};

extern const OptList DIM_OPTS, OFF_OPTS, LED_OPTS, LED_QUIET_OPTS, LED_SCREEN_OFF_OPTS, CLICK_OPTS, NOTIFY_OPTS, SPEAK_OPTS, MIC_GAIN_OPTS,
    STATS_OPTS, CPU_OPTS, TZ_NAMES, PTT_OPTS, TERM_FONT_OPTS, TEXT_SIZE_OPTS, DASH_CYCLE_OPTS,
    CPU_SAVE_OPTS, WIFI_SAVE_OPTS, STATS_DIM_OPTS, STATS_OFF_OPTS, DIM_LEVEL_OPTS, QUIET_OPTS, NOTIFY_DONE_OPTS, REMIND_OPTS, REPEAT_DELAY_OPTS, REPEAT_RATE_OPTS;
extern const uint32_t DIM_MS[], OFF_MS[], STATS_MS[], DASH_CYCLE_MS[], STATS_DIM_MS[], STATS_OFF_MS[], REMIND_MS[], REPEAT_DELAY_MS[], REPEAT_RATE_MS[];
extern const uint8_t MIC_GAIN_VALUES[], QUIET_FROM[], QUIET_TO[];
extern const uint16_t NOTIFY_DONE_SECS[];
extern const uint16_t CPU_VALUES[];
extern const char* const TZ_POSIX[];

std::string settingsToJson(const Settings& s);
bool settingsFromJson(Settings& s, const std::string& json);

/** Persistence backend: NVS on the device, memory in the snapshot tool. */
class SettingsStore {
 public:
  virtual ~SettingsStore() {}
  virtual std::string load(const char* key) = 0;
  virtual void save(const char* key, const std::string& value) = 0;
  virtual void wipe() = 0;
};

Settings& settings();
void settingsBind(SettingsStore* store);
void settingsLoad();
void settingsSave();

std::string toHex(const uint8_t* data, size_t len);
bool fromHex(const std::string& hex, uint8_t* out, size_t len);
