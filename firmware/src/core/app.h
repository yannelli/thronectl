#pragma once
#include "platform.h"
#include "input.h"
#include "services.h"
#include "updater.h"
#include "voice.h"

/** Glue between services, the store and the UI. */
class App {
 public:
  void begin();
  void loop(uint32_t now);

  void onKey(const KeyEvent& e);
  void onPtt(bool down);
  void onLinkEvent(const char* ev, JsonVariantConst d);
  void onSpeaker(const uint8_t* pcm, size_t bytes);
  void onOtaData(const uint8_t* data, size_t len);

  void activity();
  bool screenOn() const { return !screenOff_; }
  enum class Display : uint8_t { Active, Dimmed, Off };
  Display display() const { return screenOff_ ? Display::Off : dimmed_ ? Display::Dimmed : Display::Active; }
  /** Stats interval for the current display state and battery; 0 pauses stats. */
  uint32_t statsInterval() const;
  /** True while the low battery saver runs. */
  bool lowBattery() const { return lowBatt_; }
  /** Main loop sleep: longer while the screen is off, so the idle task can rest the CPU. */
  uint32_t loopDelayMs() const;
  /** Milliseconds between frames: fast while the user is active or something animates. */
  uint32_t frameInterval(uint32_t now) const;
  void applyDisplay();
  void applySound();
  /** Do not disturb, or inside the quiet hours: alerts stay silent and do not wake the screen. */
  bool quietNow() const;

  void refreshHarness(bool force, std::function<void()> done = nullptr);
  void refreshProjects(bool force, std::function<void()> done = nullptr);
  void resubscribe();
  void speakAgent(const std::string& id);
  void connectDefault();
  /** Seconds until the next automatic reconnect, or -1 when none is planned. */
  int reconnectIn(uint32_t now) const;
  void manualDisconnect();

  /** Receives "term" and "term_exit" events for the open terminal screen. */
  std::function<void(const char* ev, JsonVariantConst d)> termSink;
  Voice voice;

 private:
  void onLinkState(LinkState s);
  void handleNotify(JsonVariantConst d);
  void offerUpdate(JsonVariantConst d);
  void updateLed(uint32_t now);
  void updatePower(uint32_t now);
  void checkBattery(uint32_t now);
  void remindWaiting(uint32_t now);
  bool alertWanted(const std::string& kind, int secs) const;
  void nudge(int dVolume, int dBrightness);

  LinkState lastState_ = LinkState::Offline;
  uint32_t lastActivity_ = 0;
  bool dimmed_ = false, screenOff_ = false, manualOffline_ = false;
  uint32_t nextReconnect_ = 0, backoff_ = 2000;
  uint32_t ledFlashUntil_ = 0, termAt_ = 0;
  uint16_t ledFlashColor_ = 0;
  UpdatePhase updatePhase_ = UpdatePhase::Idle;
  uint32_t powerKey_ = 0, statsMs_ = 0, battAt_ = 0, shutdownAt_ = 0;
  bool lowBatt_ = false, battWarned_ = false;
  uint32_t remindAt_ = 0;
};

App& app();
