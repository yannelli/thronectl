// Runs src/hal/ble.cpp against a fake NimBLE: advertising, the credit window, ring wrap, and reconnects.
#include <cstdio>
#include <vector>
#include <Arduino.h>
#include "hal/hal.h"
#include "NimBLEDevice.h"

uint32_t g_millis = 1000;
EspStub ESP;
FakeRadio radio;
uint32_t nowMs() { return g_millis; }

static int failures = 0;
static void check(bool ok, const char* what) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}

static const char* RX = "7b3e0002-6f1a-4c2e-9d6a-2f1c8e5a7d10";
static const char* TX = "7b3e0003-6f1a-4c2e-9d6a-2f1c8e5a7d10";
static const char* CREDIT = "7b3e0004-6f1a-4c2e-9d6a-2f1c8e5a7d10";

static uint32_t u32(const std::vector<uint8_t>& v) { return v[0] | v[1] << 8 | v[2] << 16 | (uint32_t)v[3] << 24; }
static uint16_t u16(const std::vector<uint8_t>& v) { return v[4] | v[5] << 8; }
static uint8_t pattern(uint32_t i) { return (uint8_t)(i * 7 + (i >> 9)); }

/** Last credit notification, or an empty vector. */
static std::vector<uint8_t> lastCredit() {
  for (auto it = radio.notes.rbegin(); it != radio.notes.rend(); ++it)
    if (it->uuid == CREDIT) return it->data;
  return {};
}

/** The host sends `total` bytes in MTU-3 writes within a 12 KB window; the link reads in 1460-byte pieces. */
static bool stream(BleHal& ble, uint32_t total, uint32_t& maxUnread) {
  uint32_t sent = 0, got = 0;
  uint8_t pkt[244], buf[1460];
  while (got < total) {
    uint32_t consumed = lastCredit().empty() ? 0 : u32(lastCredit());
    while (sent < total && sent - consumed + 244 <= 12288) {
      uint16_t n = (uint16_t)std::min<uint32_t>(244, total - sent);
      for (uint16_t i = 0; i < n; i++) pkt[i] = pattern(sent + i);
      radio.chars[RX]->hostWrite(pkt, n);
      sent += n;
    }
    maxUnread = std::max(maxUnread, sent - consumed);
    size_t n = ble.read(buf, sizeof buf);
    if (!n && !ble.isOpen()) return false;
    for (size_t i = 0; i < n; i++)
      if (buf[i] != pattern(got + i)) return false;
    got += n;
  }
  return true;
}

int main() {
  BleHal ble;
  uint8_t id[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  ble.setDeviceId(id);
  ble.deviceName = [] { return std::string("Cardputer-Travel-7f3a"); };

  ble.memoryKept = false;
  check(ble.needsRestart() && !ble.begin() && radio.inits == 0, "requires restart after controller memory was released");
  ble.memoryKept = true;
  check(!ble.needsRestart(), "kept controller memory permits startup");
  ESP.freeHeap = 50 * 1024;
  check(!ble.begin() && ble.error().find("Not enough free memory") == 0, "refuses to start on low memory");
  ESP.freeHeap = 200 * 1024;
  check(ble.begin() && ble.started(), "starts");

  ble.advertise(true, false);
  std::vector<uint8_t> want = {0xff, 0xff, 'T', 'C', 1, 2, 3, 4, 5, 6, 7, 8, 0};
  check(radio.advertising && radio.mfg == want, "advertises the key id");
  check(radio.name.size() <= 14, "the name fits the scan response");
  ble.advertise(true, true);
  check(radio.advertising && radio.mfg.size() == 13 && radio.mfg[12] == 1, "sets the pairing flag");
  ble.advertise(false, false);
  check(!radio.advertising, "stops advertising on request");
  ble.advertise(true, false);

  radio.server->hostConnect(1);
  radio.server->hostMtu(247);
  check(ble.hostConnected() && !radio.advertising, "the stack stops advertising on connect");
  check(u16(radio.chars[CREDIT]->value) == 247 && u32(radio.chars[CREDIT]->value) == 0, "the credit value holds the MTU");
  check(ble.accepted() == nullptr, "no pipe before the host subscribes");
  radio.chars[TX]->hostSubscribe(1);
  LinkPipe* p = ble.accepted();
  check(p == &ble && p->isOpen(), "the pipe opens after the subscription");
  check(ble.accepted() == nullptr, "the pipe is handed out once");

  uint32_t maxUnread = 0;
  check(stream(ble, 100000, maxUnread), "100 KB arrives intact through the ring");
  check(u32(lastCredit()) == 100000, "the last credit counts every byte");
  check(maxUnread <= 16384, "the window stays within the ring");
  size_t credits = 0;
  for (auto& n : radio.notes) credits += n.uuid == CREDIT;
  check(credits >= 100000 / 1460 && credits <= 100000 / 1024 + 4, "credits go out about every 1 KB");

  std::vector<uint8_t> out(5000, 0x5a);
  radio.notes.clear();
  size_t took = p->write(out.data(), out.size());
  bool sizes = !radio.notes.empty();
  for (auto& n : radio.notes) sizes = sizes && n.uuid == TX && n.data.size() <= 244 && n.conn == 1;
  check(took == 8 * 244 && sizes, "writes go out as MTU-3 notifications, 8 per call");
  radio.notifyBudget = 3;
  check(p->write(out.data(), out.size()) == 3 * 244, "a full stack takes only what it accepted");
  radio.notifyBudget = 0;
  check(p->write(out.data(), out.size()) == 0 && p->isOpen(), "no buffers: nothing sent, still open");
  radio.notifyBudget = -1;

  std::vector<uint8_t> big(244, 1);
  for (int i = 0; i < 68; i++) radio.chars[RX]->hostWrite(big.data(), 244);
  check(!p->isOpen(), "a host that ignores the window loses the link");
  p->close();
  check(radio.disconnects.size() == 1 && radio.disconnects[0] == 1, "close disconnects the host");
  p->close();
  check(radio.disconnects.size() == 1, "a second close does nothing");
  radio.server->hostDisconnect(1);
  check(!ble.hostConnected(), "disconnect clears the host");
  ble.advertise(true, false);
  check(radio.advertising, "advertises again after the host leaves");

  radio.server->hostConnect(2);
  radio.chars[TX]->hostSubscribe(1);
  p = ble.accepted();
  check(p && p->isOpen() && u32(radio.chars[CREDIT]->value) == 0, "a new connection starts with fresh counters");
  maxUnread = 0;
  radio.notes.clear();
  check(stream(ble, 5000, maxUnread), "data flows on the new connection");
  radio.server->hostDisconnect(2);
  uint8_t b[16];
  check(!p->isOpen() && p->read(b, sizeof b) == 0 && p->write(b, sizeof b) == 0, "the pipe closes when the host drops");

  radio.server->hostConnect(3);
  radio.chars[TX]->hostSubscribe(1);
  p = ble.accepted();
  ble.end();
  check(!ble.started() && !p->isOpen() && radio.deinits == 1, "end stops the stack and closes the pipe");
  check(ble.begin() && radio.inits == 2, "starts again after end");
  ble.end();

  printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
  return failures ? 1 : 0;
}
