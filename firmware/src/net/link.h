#pragma once
#include <map>
#include "../core/services.h"
#include "crypto.h"

/** Encrypted TCP link to the host daemon (docs/protocol.md). Non-blocking; call loop() often. */
class Link : public LinkApi {
 public:
  void begin(const uint8_t priv[32], const uint8_t pub[32]);
  void loop(uint32_t now);

  std::function<std::string()> deviceName;
  std::function<void(const char* ev, JsonVariantConst d)> onEvent;
  std::function<void(const uint8_t* pcm, size_t bytes)> onSpeaker;

  LinkState state() const override { return st_; }
  const std::string& error() const override { return err_; }
  const std::string& pairCode() const override { return code_; }
  const HostInfo& host() const override { return host_; }
  const std::string& targetName() const override { return targetName_; }
  void connectHost(const HostEntry& h) override;
  void connectNew(const std::string& addr, uint16_t port, const std::string& nameHint) override;
  void disconnect() override;
  uint32_t call(const char* method, JsonDocument& params, RpcCallback cb, uint32_t timeoutMs = 20000) override;
  using LinkApi::call;
  bool sendMic(const int16_t* pcm, size_t samples) override;
  uint32_t rttMs() const override { return rtt_; }

 private:
  struct Pending {
    RpcCallback cb;
    uint32_t deadline;
  };

  void start(const std::string& addr, uint16_t port);
  void fail(const std::string& why);
  void closeSocket();
  void failPending(const char* why);
  void sendFrame(const uint8_t* data, size_t len);
  void sendJsonPlain(JsonDocument& doc);
  void sendSealed(uint8_t kind, const uint8_t* body, size_t len);
  void sendHello();
  bool flush();
  bool receive();
  void handleFrame(const uint8_t* data, size_t len);
  void handleHostHello(JsonDocument& doc);
  void handleMessage(JsonDocument& doc);
  void handleAuth(JsonDocument& doc);

  LinkState st_ = LinkState::Offline;
  std::string err_, code_, targetName_, addr_;
  uint16_t port_ = 0;
  HostEntry target_;
  bool haveTarget_ = false;
  HostInfo host_;
  uint8_t priv_[32] = {0}, pub_[32] = {0};
  uint8_t epriv_[32] = {0}, epub_[32] = {0}, dn_[16] = {0}, hpk_[32] = {0};
  tcrypto::SessionKeys keys_{};
  bool sealed_ = false;
  uint64_t txCtr_ = 0, rxCtr_ = 0;
  int fd_ = -1;
  std::vector<uint8_t> rx_;
  std::string tx_;
  std::map<uint32_t, Pending> pending_;
  std::vector<RpcCallback> deferred_;
  uint32_t nextId_ = 1, stateAt_ = 0, lastRx_ = 0, lastPing_ = 0, pingAt_ = 0, rtt_ = 0;
};
