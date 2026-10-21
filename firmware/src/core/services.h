#pragma once
#include "platform.h"
#include "settings.h"
#include <ArduinoJson.h>

enum class LinkState : uint8_t { Offline, Connecting, Handshake, Pairing, Ready, Failed };

struct HostInfo {
  std::string id, name, hostname, os, arch, user, ver;
  bool paseo = false;
  bool hasTz = false;
  int tzOffMin = 0;
};

using RpcCallback = std::function<void(bool ok, JsonVariantConst result, const char* err)>;

class LinkApi {
 public:
  virtual ~LinkApi() {}
  virtual LinkState state() const = 0;
  virtual const std::string& error() const = 0;
  virtual const std::string& pairCode() const = 0;
  virtual const HostInfo& host() const = 0;
  virtual const std::string& targetName() const = 0;
  virtual void connectHost(const HostEntry& h) = 0;
  virtual void connectNew(const std::string& addr, uint16_t port, const std::string& nameHint) = 0;
  virtual void disconnect() = 0;
  virtual uint32_t call(const char* method, JsonDocument& params, RpcCallback cb, uint32_t timeoutMs = 20000) = 0;
  virtual bool sendMic(const int16_t* pcm, size_t samples) = 0;
  virtual uint32_t rttMs() const = 0;

  uint32_t call(const char* method, RpcCallback cb) {
    JsonDocument d;
    d.to<JsonObject>();
    return call(method, d, cb);
  }
  bool ready() const { return state() == LinkState::Ready; }
};

struct Beacon {
  std::string id, name, ip, os;
  uint16_t port = THRONE_PORT;
};

struct WifiScanItem {
  std::string ssid;
  int rssi = 0;
  bool secure = true;
};

enum class WifiState : uint8_t { Off, Connecting, Connected, Failed };

class NetApi {
 public:
  virtual ~NetApi() {}
  virtual WifiState wifiState() const = 0;
  virtual std::string ssid() const = 0;
  virtual std::string ip() const = 0;
  virtual int rssi() const = 0;
  virtual std::string mac() const = 0;
  virtual void connectWifi(const std::string& ssid, const std::string& pass) = 0;
  virtual void connectSaved() = 0;
  virtual void disconnectWifi() = 0;
  virtual void startScan() = 0;
  virtual bool scanning() const = 0;
  virtual const std::vector<WifiScanItem>& scanResults() const = 0;
  virtual void startDiscovery() = 0;
  virtual bool discovering() const = 0;
  virtual const std::vector<Beacon>& beacons() const = 0;
};

enum Chime : uint8_t { CH_STARTUP, CH_DONE, CH_ATTN, CH_ERROR, CH_INFO, CH_OK, CH_FAIL, CH_REC_START, CH_REC_STOP };

class AudioApi {
 public:
  virtual ~AudioApi() {}
  virtual void setVolume(uint8_t v) = 0;
  virtual void click() = 0;
  virtual void chime(Chime kind) = 0;
  virtual bool micStart(uint8_t gain) = 0;
  virtual void micStop() = 0;
  virtual bool micActive() const = 0;
  virtual float micLevel() const = 0;
  virtual void micPoll(const std::function<void(const int16_t*, size_t)>& sink) = 0;
  virtual void speakerBegin(uint32_t rate) = 0;
  virtual void speakerFeed(const uint8_t* pcm, size_t bytes) = 0;
  virtual void speakerEnd() = 0;
  virtual void speakerStop() = 0;
  virtual bool speaking() const = 0;
};

class SystemApi {
 public:
  virtual ~SystemApi() {}
  virtual int batteryPct() const = 0;
  virtual bool charging() const = 0;
  virtual float batteryVolts() const = 0;
  virtual void setBrightness(uint8_t b) = 0;
  virtual void setRotationFlipped(bool flipped) = 0;
  virtual void setLed(uint16_t color565, uint8_t brightness) = 0;
  virtual void setCpuMhz(uint16_t mhz) = 0;
  virtual uint32_t freeHeap() const = 0;
  virtual uint32_t minFreeHeap() const = 0;
  virtual std::string chipInfo() const = 0;
  virtual std::string deviceId() const = 0;
  virtual uint32_t uptimeS() const = 0;
  virtual bool localTime(int& h, int& m, int& s) const = 0;
  virtual void setTimezone(const char* posix) = 0;
  virtual void setTimeFromHost(int64_t unixMs) = 0;
  virtual void reboot() = 0;
  virtual void factoryReset() = 0;
  virtual void powerOff() = 0;
};

struct Services {
  LinkApi* link = nullptr;
  NetApi* net = nullptr;
  AudioApi* audio = nullptr;
  SystemApi* sys = nullptr;
};

Services& svc();
