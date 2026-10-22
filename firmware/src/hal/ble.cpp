#include <Arduino.h>
#include <NimBLEDevice.h>
#include "hal.h"

// The daemon's src/ble/uuids.ts holds the same values. Hosts cache this layout, so keep it fixed.
static const char* SERVICE_UUID = "7b3e0001-6f1a-4c2e-9d6a-2f1c8e5a7d10";
static const char* RX_UUID = "7b3e0002-6f1a-4c2e-9d6a-2f1c8e5a7d10";
static const char* TX_UUID = "7b3e0003-6f1a-4c2e-9d6a-2f1c8e5a7d10";
static const char* CREDIT_UUID = "7b3e0004-6f1a-4c2e-9d6a-2f1c8e5a7d10";
static constexpr size_t RING = 16384;          // the host keeps at most 12 KB unread
static constexpr uint32_t CREDIT_STEP = 1024;  // report reads at least this often
static constexpr int PACKETS_PER_WRITE = 8;
static constexpr uint32_t MIN_FREE_HEAP = 90 * 1024;
static constexpr uint32_t MIN_HEAP_AFTER = 32 * 1024;

static BleHal* hal = nullptr;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

namespace {

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    uint16_t h = info.getConnHandle();
    hal->onConnect(h);
    s->updateConnParams(h, 12, 24, 0, 600);  // 15 to 30 ms, 6 s supervision timeout
    s->setDataLen(h, 251);
    s->updatePhy(h, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK, 0);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override { hal->onDisconnect(info.getConnHandle()); }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo&) override { hal->onMtu(mtu); }
};

class SubscribeCb : public NimBLECharacteristicCallbacks {
 public:
  explicit SubscribeCb(bool tx) : tx_(tx) {}
  void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo&, uint16_t value) override { hal->onSubscribe(tx_, value & 1); }

 private:
  bool tx_;
};

/** Takes host writes straight from the stack's buffer, without storing them as the characteristic value. */
class RxChr : public NimBLECharacteristic {
 public:
  explicit RxChr(NimBLEService* s)
      : NimBLECharacteristic(RX_UUID, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE, 512, s) {}

 private:
  void writeEvent(const uint8_t* val, uint16_t len, NimBLEConnInfo&) override { hal->onRx(val, len); }
};

ServerCb serverCb;
SubscribeCb txCb(true), creditCb(false);

void putCredit(uint8_t out[6], uint32_t consumed, uint16_t mtu) {
  for (int i = 0; i < 4; i++) out[i] = consumed >> (8 * i);
  out[4] = mtu;
  out[5] = mtu >> 8;
}

}  // namespace

bool BleHal::fail(const std::string& e) {
  err_ = e;
  return false;
}

bool BleHal::begin() {
  if (started_) return true;
  err_.clear();
  if (!memoryKept) return fail("Restart the device to turn on Bluetooth.");
  uint32_t free = ESP.getFreeHeap();
  if (free < MIN_FREE_HEAP)
    return fail("Not enough free memory (" + std::to_string(free / 1024) + " KB). Bluetooth needs about 90 KB.");
  ring_.reset(new (std::nothrow) uint8_t[RING]);
  if (!ring_) return fail("Not enough free memory for the Bluetooth buffer.");
  hal = this;
  std::string name = deviceName ? deviceName() : std::string("Cardputer");
  if (!NimBLEDevice::init(name.substr(0, 20))) {
    NimBLEDevice::deinit(true);
    ring_.reset();
    return fail("The Bluetooth stack did not start.");
  }
  NimBLEDevice::setMTU(247);
  srv_ = NimBLEDevice::createServer();
  srv_->setCallbacks(&serverCb, false);
  srv_->advertiseOnDisconnect(false);
  NimBLEService* s = srv_->createService(SERVICE_UUID);
  s->addCharacteristic(new RxChr(s));
  tx_ = s->createCharacteristic(TX_UUID, NIMBLE_PROPERTY::NOTIFY, 512);
  tx_->setCallbacks(&txCb);
  credit_ = s->createCharacteristic(CREDIT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 6);
  credit_->setCallbacks(&creditCb);
  uint8_t c[6];
  putCredit(c, 0, 23);
  credit_->setValue(c, 6);
  started_ = true;
  if (ESP.getFreeHeap() < MIN_HEAP_AFTER) {
    end();
    return fail("Bluetooth used too much memory and was stopped.");
  }
  return true;
}

void BleHal::end() {
  if (!started_) return;
  NimBLEDevice::deinit(true);
  portENTER_CRITICAL(&mux);
  conn_ = NO_CONN;
  gen_++;
  txSub_ = false;
  portEXIT_CRITICAL(&mux);
  srv_ = nullptr;
  tx_ = credit_ = nullptr;
  ring_.reset();
  started_ = false;
}

void BleHal::advertise(bool on, bool pairing) {
  if (!started_) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  bool active = adv->isAdvertising();
  if (!on || conn_ != NO_CONN) {
    if (active && !on) adv->stop();
    return;
  }
  if (active && pairing == advPairing_) return;
  if (!active && (int32_t)(millis() - advRetryAt_) < 0) return;
  if (active) adv->stop();
  // The scan response carries what the daemon matches: "TC", the key id, and the pairing flag.
  uint8_t m[13] = {0xff, 0xff, 'T', 'C'};
  memcpy(m + 4, id_, 8);
  m[12] = pairing ? 1 : 0;
  NimBLEAdvertisementData ad, scan;
  ad.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  ad.addServiceUUID(NimBLEUUID(SERVICE_UUID));
  scan.setManufacturerData(m, sizeof m);
  std::string name = deviceName ? deviceName() : std::string("Cardputer");
  scan.setName(name.substr(0, 14), name.size() <= 14);
  adv->setAdvertisementData(ad);
  adv->setScanResponseData(scan);
  adv->setMinInterval(pairing ? 80 : 320);  // 50 or 200 ms, in 0.625 ms units
  adv->setMaxInterval(pairing ? 160 : 480);
  advPairing_ = pairing;
  if (!adv->start()) advRetryAt_ = millis() + 1000;
}

LinkPipe* BleHal::accepted() {
  bool ready = false;
  portENTER_CRITICAL(&mux);
  if (conn_ != NO_CONN && txSub_ && handed_ != gen_) {
    handed_ = gen_;
    ready = true;
  }
  portEXIT_CRITICAL(&mux);
  if (!ready) return nullptr;
  credited_ = 0;
  creditMtu_ = 0;
  return this;
}

bool BleHal::isOpen() const {
  return started_ && handed_ == gen_ && closed_ != gen_ && conn_ != NO_CONN && !overflow_;
}

size_t BleHal::write(const uint8_t* data, size_t len) {
  portENTER_CRITICAL(&mux);
  bool ok = isOpen() && txSub_;
  uint16_t conn = conn_, mtu = mtu_;
  portEXIT_CRITICAL(&mux);
  if (!ok) return 0;
  size_t chunk = mtu > 23 ? mtu - 3 : 20, sent = 0;
  // notify() fails when the stack has no free buffers; the link sends the rest on a later loop.
  for (int i = 0; i < PACKETS_PER_WRITE && sent < len; i++) {
    size_t n = std::min(chunk, len - sent);
    if (!tx_->notify(data + sent, n, conn)) break;
    sent += n;
  }
  return sent;
}

size_t BleHal::read(uint8_t* data, size_t len) {
  size_t n = 0;
  portENTER_CRITICAL(&mux);
  bool ok = isOpen();
  uint32_t consumed = out_, pending = in_ - out_;
  uint16_t conn = conn_;
  if (ok) {
    n = std::min(len, (size_t)pending);
    size_t at = out_ % RING, first = std::min(n, RING - at);
    memcpy(data, ring_.get() + at, first);
    memcpy(data + first, ring_.get(), n - first);
    out_ += n;
    consumed = out_;
    pending -= n;
  }
  portEXIT_CRITICAL(&mux);
  if (!ok) return 0;
  bool due = consumed - credited_ >= CREDIT_STEP || (pending == 0 && consumed != credited_) || creditMtu_ != mtu_;
  if (due) sendCredit(consumed, conn);
  return n;
}

void BleHal::sendCredit(uint32_t consumed, uint16_t conn) {
  uint8_t c[6];
  uint16_t mtu = mtu_;
  putCredit(c, consumed, mtu);
  credit_->setValue(c, 6);
  if (!credit_->notify(c, 6, conn)) return;  // no free buffer: the next read tries again
  credited_ = consumed;
  creditMtu_ = mtu;
}

void BleHal::close() {
  uint16_t conn = NO_CONN;
  portENTER_CRITICAL(&mux);
  if (handed_ == gen_ && closed_ != gen_) {
    closed_ = gen_;
    conn = conn_;
  }
  portEXIT_CRITICAL(&mux);
  if (conn != NO_CONN && srv_) srv_->disconnect(conn);
}

void BleHal::onConnect(uint16_t conn) {
  portENTER_CRITICAL(&mux);
  gen_++;
  conn_ = conn;
  in_ = out_ = 0;
  overflow_ = false;
  txSub_ = false;
  portEXIT_CRITICAL(&mux);
  mtu_ = 23;
  uint8_t c[6];
  putCredit(c, 0, 23);
  credit_->setValue(c, 6);
}

void BleHal::onDisconnect(uint16_t conn) {
  portENTER_CRITICAL(&mux);
  if (conn_ == conn) {
    conn_ = NO_CONN;
    txSub_ = false;
  }
  portEXIT_CRITICAL(&mux);
}

void BleHal::onMtu(uint16_t mtu) {
  mtu_ = mtu;
  uint8_t c[6];
  putCredit(c, out_, mtu);
  credit_->setValue(c, 6);
}

void BleHal::onSubscribe(bool tx, bool on) {
  if (tx) txSub_ = on;
}

void BleHal::onRx(const uint8_t* data, size_t len) {
  portENTER_CRITICAL(&mux);
  if (in_ - out_ + len > RING || !ring_) {
    overflow_ = true;  // the host ignored the credit window
  } else {
    size_t at = in_ % RING, first = std::min(len, RING - at);
    memcpy(ring_.get() + at, data, first);
    memcpy(ring_.get(), data + first, len - first);
    in_ += len;
  }
  portEXIT_CRITICAL(&mux);
}
