#pragma once
#include "../core/input.h"
#include "../core/services.h"

/** Reads the TCA8418 matrix through M5Cardputer and emits key presses with auto-repeat. */
class KeyboardHal {
 public:
  void poll(uint32_t now, const std::function<void(const KeyEvent&)>& emit);

 private:
  struct Pos {
    int8_t x, y;
    bool operator==(const Pos& o) const { return x == o.x && y == o.y; }
  };
  bool makeEvent(Pos p, KeyEvent& e) const;
  std::vector<Pos> prev_;
  Pos repeatPos_{-1, -1};
  uint32_t repeatAt_ = 0;
  bool fn_ = false, shift_ = false, ctrl_ = false, opt_ = false, alt_ = false;
};

/** Speaker and microphone on the shared ES8311 I2S bus. Only one direction runs at a time. */
class AudioHal : public AudioApi {
 public:
  void begin();
  void loop(uint32_t now);

  void setVolume(uint8_t v) override;
  void click() override;
  void chime(Chime kind) override;
  bool micStart(uint8_t gain) override;
  void micStop() override;
  bool micActive() const override { return mic_; }
  float micLevel() const override { return level_; }
  void micPoll(const std::function<void(const int16_t*, size_t)>& sink) override;
  void speakerBegin(uint32_t rate) override;
  void speakerFeed(const uint8_t* pcm, size_t bytes) override;
  void speakerEnd() override;
  void speakerStop() override;
  bool speaking() const override { return streaming_; }

 private:
  struct Note {
    uint16_t hz, ms;
  };
  void pumpStream();

  bool mic_ = false;
  float level_ = 0;
  static constexpr int MIC_CHUNK = 800, MIC_BUFS = 3;
  int16_t* micBuf_ = nullptr;
  int micHead_ = 0, micQueued_ = 0;

  static constexpr size_t RING = 16000, OUT_CHUNK = 1600, OUT_BUFS = 3;
  int16_t* ring_ = nullptr;
  int16_t* out_ = nullptr;
  size_t rHead_ = 0, rLen_ = 0, outIdx_ = 0;
  uint32_t rate_ = 16000;
  bool streaming_ = false, ended_ = false, primed_ = false;

  std::vector<Note> notes_;
  size_t noteIdx_ = 0;
  uint32_t noteAt_ = 0;
};

class SystemHal : public SystemApi {
 public:
  void begin();
  void loop(uint32_t now);

  int batteryPct() const override { return batt_; }
  bool charging() const override { return chg_; }
  float batteryVolts() const override { return volts_; }
  void setBrightness(uint8_t b) override;
  void setRotationFlipped(bool flipped) override;
  void setLed(uint16_t color565, uint8_t brightness) override;
  void setCpuMhz(uint16_t mhz) override;
  uint32_t freeHeap() const override;
  uint32_t minFreeHeap() const override;
  std::string chipInfo() const override;
  std::string deviceId() const override;
  uint32_t uptimeS() const override;
  bool localTime(int& h, int& m, int& s) const override;
  void setTimezone(const char* posix) override;
  void setTimeFromHost(int64_t unixMs) override;
  void reboot() override;
  void factoryReset() override;
  void powerOff() override;
  void startNtp();

 private:
  int batt_ = -1;
  bool chg_ = false;
  float volts_ = 0;
  uint32_t battAt_ = 0;
  int brightness_ = -1, rotation_ = -1;
  uint16_t ledColor_ = 1;
  uint8_t ledLevel_ = 1;
};

/** Settings and keys in NVS. */
class NvsStore : public SettingsStore {
 public:
  std::string load(const char* key) override;
  void save(const char* key, const std::string& value) override;
  void wipe() override;
  bool loadBlob(const char* key, uint8_t* out, size_t len);
  void saveBlob(const char* key, const uint8_t* data, size_t len);
};
