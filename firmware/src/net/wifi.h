#pragma once
#include <WiFiUdp.h>
#include "../core/services.h"

/** Station Wi-Fi with saved-network fallback, async scans and UDP host discovery. */
class Net : public NetApi {
 public:
  void begin(const std::string& hostname);
  void loop(uint32_t now);

  WifiState wifiState() const override { return st_; }
  std::string ssid() const override { return ssid_; }
  std::string ip() const override;
  int rssi() const override;
  std::string mac() const override;
  void connectWifi(const std::string& ssid, const std::string& pass) override;
  void connectSaved() override;
  void disconnectWifi() override;
  void startScan() override;
  bool scanning() const override { return scanning_; }
  const std::vector<WifiScanItem>& scanResults() const override { return scan_; }
  void startDiscovery() override;
  bool discovering() const override { return discoverUntil_ != 0; }
  const std::vector<Beacon>& beacons() const override { return beacons_; }

  /** Called once each time the station gets an IP. */
  std::function<void()> onConnected;

 private:
  void tryIndex(size_t i);

  WifiState st_ = WifiState::Off;
  std::string ssid_, pass_;
  bool fromSaved_ = false, scanning_ = false, wasUp_ = false;
  size_t savedIdx_ = 0;
  uint32_t attemptAt_ = 0, retryAt_ = 0;
  std::vector<WifiScanItem> scan_;
  WiFiUDP udp_;
  bool udpOpen_ = false;
  uint32_t discoverUntil_ = 0, probeAt_ = 0;
  int probes_ = 0;
  std::vector<Beacon> beacons_;
};
