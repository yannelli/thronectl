#pragma once
#include <FS.h>
#include <memory>
#include "../core/filestore.h"
#include "../core/input.h"
#include "../core/services.h"
#include "../net/crypto.h"
#include "led.h"

/** Reads the TCA8418 matrix through M5Cardputer and emits key presses with auto-repeat. */
class KeyboardHal {
 public:
  void poll(uint32_t now, const std::function<void(const KeyEvent&)>& emit);

 private:
  struct Pos {
    int8_t x, y;
    bool operator==(const Pos& o) const { return x == o.x && y == o.y; }
  };
  void scan(uint32_t now, const std::function<void(const KeyEvent&)>& emit);
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
  void setMix(uint8_t alerts, uint8_t voice) override;
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
  void setPanelSleep(bool asleep) override;
  void setRotationFlipped(bool flipped) override;
  void setLed(Rgb c) override;
  void setLedPower(bool powered) override;
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
  std::string storageInfo() const override;
  void startNtp();

 private:
  int batt_ = -1;
  bool chg_ = false, rising_ = false, asleep_ = false;
  float volts_ = 0, trendV_ = 0;
  uint32_t battAt_ = 0, trendAt_ = 0;
  int rotation_ = -1;
  LedOutput led_;
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

/** Settings files on internal flash: LittleFS on the 1.5 MB "spiffs" data partition, mounted at /flash. */
class FlashStore : public FileStore {
 public:
  FlashStore() : FileStore("/flash/thronectl") {}
  /** Mounts the volume. With mayFormat, formats one that does not mount, such as a blank partition. */
  static bool mount(bool mayFormat);
  /** True when the partition was never written, so formatting it cannot erase another app's files. */
  static bool blank();
  /** Space used and total, or "" when the volume is not mounted. */
  static std::string usage();
};

/** Firmware updates into the spare app slot (Arduino Update), or onto the microSD card for Launcher. */
class OtaHal : public OtaApi {
 public:
  bool underLauncher() const override;
  bool sdReady() override;
  std::vector<SdImage> sdImages() override;
  bool begin(uint32_t size, const std::string& sha256, const std::string& sdPath) override;
  bool write(const uint8_t* data, size_t len) override;
  bool finish() override;
  void abort() override;
  const std::string& error() const override { return err_; }
  uint32_t openSd(const std::string& path) override;
  int readSd(uint8_t* buf, size_t len) override;
  void closeSd() override;
  bool sdWriteFile(const std::string& path, const std::string& data) override;
  bool sdReadFile(const std::string& path, std::string& out) override;
  bool sdList(const std::string& dir, std::vector<SdEntry>& out) override;
  bool sdMkdir(const std::string& path) override;
  bool sdNeedsFormat() const override { return needsFormat_; }
  bool sdFormat() override;

 private:
  enum class Sink : uint8_t { None, Flash, Sd };
  bool setErr(const std::string& e);
  uint32_t openFail(const std::string& e);
  bool mount(bool format);
  bool sdIdle();
  bool writeOnce(const std::string& path, const std::string& data);

  Sink sink_ = Sink::None;
  std::string err_, sha_, sdPath_;
  std::unique_ptr<tcrypto::Sha256> hash_;
  uint32_t written_ = 0;
  bool spiBegun_ = false, mounted_ = false, needsFormat_ = false;
  fs::File in_, out_;
};

class NimBLEServer;
class NimBLECharacteristic;

/** Bluetooth LE peripheral (NimBLE) for one host. The GATT service carries the link's byte stream with credit flow control. */
class BleHal : public BleApi, public LinkPipe {
 public:
  std::function<std::string()> deviceName;
  void setDeviceId(const uint8_t id[8]) { memcpy(id_, id, 8); }
  bool begin() override;
  void end() override;
  bool started() const override { return started_; }
  void advertise(bool on, bool pairing) override;
  LinkPipe* accepted() override;
  bool hostConnected() const override { return conn_ != NO_CONN; }
  const std::string& error() const override { return err_; }
  bool needsRestart() const override { return !memoryKept; }
  bool memoryKept = true;  // false when Arduino freed the controller's memory at boot

  bool isOpen() const override;
  size_t write(const uint8_t* data, size_t len) override;
  size_t read(uint8_t* data, size_t len) override;
  void close() override;

  // From the NimBLE host task.
  void onConnect(uint16_t conn);
  void onDisconnect(uint16_t conn);
  void onMtu(uint16_t mtu);
  void onSubscribe(bool tx, bool on);
  void onRx(const uint8_t* data, size_t len);

 private:
  static constexpr uint16_t NO_CONN = 0xFFFF;
  void sendCredit(uint32_t consumed, uint16_t conn);
  bool fail(const std::string& e);
  NimBLEServer* srv_ = nullptr;
  NimBLECharacteristic *tx_ = nullptr, *credit_ = nullptr;
  std::unique_ptr<uint8_t[]> ring_;
  // Shared with the host task under a spinlock. gen_ counts connections; the pipe is open while handed_ == gen_.
  volatile uint32_t in_ = 0, out_ = 0, gen_ = 0;
  volatile uint16_t conn_ = NO_CONN, mtu_ = 23;
  volatile bool txSub_ = false, overflow_ = false;
  uint32_t handed_ = 0, closed_ = 0, credited_ = 0, advRetryAt_ = 0;
  uint16_t creditMtu_ = 0;
  bool started_ = false, advPairing_ = false;
  uint8_t id_[8] = {};
  std::string err_;
};
