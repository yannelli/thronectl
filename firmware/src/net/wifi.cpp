#include "wifi.h"
#include <WiFi.h>
#include <algorithm>
#include "../core/settings.h"

static constexpr uint32_t JOIN_TIMEOUT_MS = 15000;

void Net::begin(const std::string& hostname) {
  WiFi.setHostname(hostname.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
}

std::string Net::ip() const { return st_ == WifiState::Connected ? WiFi.localIP().toString().c_str() : ""; }
int Net::rssi() const { return st_ == WifiState::Connected ? WiFi.RSSI() : -100; }
std::string Net::mac() const { return WiFi.macAddress().c_str(); }

void Net::connectWifi(const std::string& ssid, const std::string& pass) {
  fromSaved_ = false;
  ssid_ = ssid;
  pass_ = pass;
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), pass.empty() ? nullptr : pass.c_str());
  st_ = WifiState::Connecting;
  attemptAt_ = millis();
}

void Net::tryIndex(size_t i) {
  auto& list = settings().wifi;
  if (i >= list.size()) {
    st_ = list.empty() ? WifiState::Off : WifiState::Failed;
    retryAt_ = millis() + 30000;
    return;
  }
  savedIdx_ = i;
  connectWifi(list[i].ssid, list[i].pass);
  fromSaved_ = true;
}

void Net::connectSaved() {
  if (settings().wifi.empty()) return;
  tryIndex(0);
}

void Net::disconnectWifi() {
  WiFi.disconnect(false, false);
  st_ = WifiState::Off;
  retryAt_ = 0;
}

void Net::startScan() {
  if (scanning_) return;
  scanning_ = WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING;
}

void Net::startDiscovery() {
  if (st_ != WifiState::Connected) return;
  if (!udpOpen_) udpOpen_ = udp_.begin(0);
  discoverUntil_ = millis() + 2500;
  probes_ = 0;
  probeAt_ = 0;
}

void Net::loop(uint32_t now) {
  bool up = WiFi.status() == WL_CONNECTED;
  if (up && st_ != WifiState::Connected && st_ != WifiState::Off) {
    st_ = WifiState::Connected;
    ssid_ = WiFi.SSID().c_str();
  } else if (!up && st_ == WifiState::Connected) {
    st_ = WifiState::Connecting;
    attemptAt_ = now;
  }
  if (up && !wasUp_ && onConnected) onConnected();
  wasUp_ = up;

  if (st_ == WifiState::Connecting && elapsedMs(now, attemptAt_) > JOIN_TIMEOUT_MS) {
    if (fromSaved_) tryIndex(savedIdx_ + 1);
    else {
      st_ = WifiState::Failed;
      retryAt_ = now + 30000;
    }
  }
  if (st_ == WifiState::Failed && retryAt_ && (int32_t)(now - retryAt_) > 0) {
    retryAt_ = 0;
    connectSaved();
  }

  if (scanning_) {
    int n = WiFi.scanComplete();
    if (n != WIFI_SCAN_RUNNING) {
      scanning_ = false;
      scan_.clear();
      for (int i = 0; i < n; i++) scan_.push_back({WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
      std::sort(scan_.begin(), scan_.end(), [](const WifiScanItem& a, const WifiScanItem& b) { return a.rssi > b.rssi; });
      WiFi.scanDelete();
    }
  }

  if (!udpOpen_) return;
  if (discoverUntil_ && probes_ < 3 && now >= probeAt_) {
    udp_.beginPacket(IPAddress(255, 255, 255, 255), THRONE_PORT);
    udp_.print("THRONE?1");
    udp_.endPacket();
    probes_++;
    probeAt_ = now + 600;
  }
  if (discoverUntil_ && (int32_t)(now - discoverUntil_) > 0) discoverUntil_ = 0;
  for (int i = 0; i < 4; i++) {
    int len = udp_.parsePacket();
    if (len <= 0) break;
    char buf[512];
    int n = udp_.read(buf, sizeof buf - 1);
    if (n <= 0) continue;
    buf[n] = 0;
    JsonDocument d;
    if (deserializeJson(d, buf) || strcmp(d["t"] | "", "throne") != 0) continue;
    Beacon b;
    b.id = d["id"] | "";
    b.name = d["name"] | "host";
    b.os = d["os"] | "";
    b.port = d["port"] | THRONE_PORT;
    b.ip = udp_.remoteIP().toString().c_str();
    bool found = false;
    for (auto& x : beacons_)
      if (x.id == b.id) x = b, found = true;
    if (!found && !b.id.empty()) beacons_.push_back(b);
  }
}
