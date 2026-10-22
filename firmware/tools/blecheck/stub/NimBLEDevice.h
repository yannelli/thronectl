#pragma once
// A small fake of NimBLE-Arduino 2.x: it records what the HAL does and lets the test play the host.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define BLE_GAP_LE_PHY_2M_MASK 0x02
#define BLE_HS_ADV_F_DISC_GEN 0x02
#define BLE_HS_ADV_F_BREDR_UNSUP 0x04
enum NIMBLE_PROPERTY : uint16_t { READ = 0x02, WRITE_NR = 0x04, WRITE = 0x08, NOTIFY = 0x10 };

struct NimBLEConnInfo {
  uint16_t handle = 0;
  uint16_t getConnHandle() const { return handle; }
};
struct NimBLEUUID {
  std::string s;
  NimBLEUUID(const char* u) : s(u) {}
};

class NimBLECharacteristic;
class NimBLEServer;
class NimBLEService;
struct NimBLEServerCallbacks {
  virtual ~NimBLEServerCallbacks() {}
  virtual void onConnect(NimBLEServer*, NimBLEConnInfo&) {}
  virtual void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) {}
  virtual void onMTUChange(uint16_t, NimBLEConnInfo&) {}
};
struct NimBLECharacteristicCallbacks {
  virtual ~NimBLECharacteristicCallbacks() {}
  virtual void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t) {}
};

struct Notified {
  std::string uuid;
  std::vector<uint8_t> data;
  uint16_t conn;
};
struct FakeRadio {
  std::vector<Notified> notes;
  int notifyBudget = -1;  // notifications to accept before failing; -1 is no limit
  std::vector<uint16_t> disconnects;
  std::map<std::string, NimBLECharacteristic*> chars;
  NimBLEServer* server = nullptr;
  bool advertising = false;
  std::vector<uint8_t> mfg;
  std::string name;
  int inits = 0, deinits = 0;
};
extern FakeRadio radio;

class NimBLECharacteristic {
 public:
  NimBLECharacteristic(const char* uuid, uint16_t props = 0, uint16_t maxLen = 512, NimBLEService* = nullptr)
      : uuid_(uuid), props_(props), max_(maxLen) {}
  virtual ~NimBLECharacteristic() {}
  void setCallbacks(NimBLECharacteristicCallbacks* cb) { cb_ = cb; }
  void setValue(const uint8_t* d, size_t n) { value.assign(d, d + n); }
  bool notify(const uint8_t* d, size_t n, uint16_t conn) const {
    if (radio.notifyBudget == 0) return false;
    if (radio.notifyBudget > 0) radio.notifyBudget--;
    radio.notes.push_back({uuid_, std::vector<uint8_t>(d, d + n), conn});
    return true;
  }
  // Test hooks: what the stack does when the host writes or subscribes.
  void hostWrite(const uint8_t* d, uint16_t n) { NimBLEConnInfo i; writeEvent(d, n, i); }
  void hostSubscribe(uint16_t v) { NimBLEConnInfo i; if (cb_) cb_->onSubscribe(this, i, v); }
  std::vector<uint8_t> value;
  std::string uuid_;
  uint16_t props_, max_;

 private:
  virtual void writeEvent(const uint8_t* d, uint16_t n, NimBLEConnInfo&) { value.assign(d, d + n); }
  NimBLECharacteristicCallbacks* cb_ = nullptr;
};

class NimBLEService {
 public:
  ~NimBLEService() { for (auto* c : chars_) delete c; }
  void addCharacteristic(NimBLECharacteristic* c) { chars_.push_back(c); radio.chars[c->uuid_] = c; }
  NimBLECharacteristic* createCharacteristic(const char* u, uint16_t p, uint16_t m) {
    auto* c = new NimBLECharacteristic(u, p, m, this);
    addCharacteristic(c);
    return c;
  }

 private:
  std::vector<NimBLECharacteristic*> chars_;
};

class NimBLEServer {
 public:
  void setCallbacks(NimBLEServerCallbacks* cb, bool) { cb_ = cb; }
  void advertiseOnDisconnect(bool) {}
  NimBLEService* createService(const char*) { svcs_.emplace_back(new NimBLEService()); return svcs_.back().get(); }
  void updateConnParams(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t) const {}
  void setDataLen(uint16_t, uint16_t) const {}
  bool updatePhy(uint16_t, uint8_t, uint8_t, uint16_t) { return true; }
  bool disconnect(uint16_t h) const { radio.disconnects.push_back(h); return true; }
  // Test hooks: the host connects, disconnects, or changes the MTU.
  void hostConnect(uint16_t h) { NimBLEConnInfo i{h}; radio.advertising = false; cb_->onConnect(this, i); }
  void hostDisconnect(uint16_t h) { NimBLEConnInfo i{h}; cb_->onDisconnect(this, i, 0x13); }
  void hostMtu(uint16_t m) { NimBLEConnInfo i; cb_->onMTUChange(m, i); }

 private:
  NimBLEServerCallbacks* cb_ = nullptr;
  std::vector<std::unique_ptr<NimBLEService>> svcs_;
};

struct NimBLEAdvertisementData {
  std::vector<uint8_t> mfg;
  std::string name;
  bool complete = true;
  bool setFlags(uint8_t) { return true; }
  bool addServiceUUID(const NimBLEUUID&) { return true; }
  bool setManufacturerData(const uint8_t* d, size_t n) { mfg.assign(d, d + n); return true; }
  bool setName(const std::string& n, bool c) { name = n; complete = c; return true; }
};

class NimBLEAdvertising {
 public:
  bool isAdvertising() { return radio.advertising; }
  bool stop() { radio.advertising = false; return true; }
  bool start() { radio.advertising = true; return true; }
  bool setAdvertisementData(const NimBLEAdvertisementData&) { return true; }
  bool setScanResponseData(const NimBLEAdvertisementData& d) { radio.mfg = d.mfg; radio.name = d.name; return true; }
  void setMinInterval(uint16_t) {}
  void setMaxInterval(uint16_t) {}
};

struct NimBLEDevice {
  static bool init(const std::string&) { radio.inits++; return true; }
  static bool deinit(bool) {
    radio.deinits++;
    delete radio.server;
    radio.server = nullptr;
    radio.chars.clear();
    return true;
  }
  static bool setMTU(uint16_t) { return true; }
  static NimBLEServer* createServer() { return radio.server = new NimBLEServer(); }
  static NimBLEAdvertising* getAdvertising() { static NimBLEAdvertising a; return &a; }
};
