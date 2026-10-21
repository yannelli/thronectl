#pragma once
#include "platform.h"
#include "input.h"
#include "services.h"
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

  void activity();
  bool screenOn() const { return !screenOff_; }
  /** Milliseconds between frames: fast while the user is active or something animates. */
  uint32_t frameInterval(uint32_t now) const;
  void applyDisplay();
  void applySound();

  void refreshHarness(bool force, std::function<void()> done = nullptr);
  void refreshProjects(bool force, std::function<void()> done = nullptr);
  void resubscribe();
  void speakAgent(const std::string& id);
  void connectDefault();
  void manualDisconnect();

  /** Receives "term" and "term_exit" events for the open terminal screen. */
  std::function<void(const char* ev, JsonVariantConst d)> termSink;
  Voice voice;

 private:
  void onLinkState(LinkState s);
  void handleNotify(JsonVariantConst d);
  void updateLed(uint32_t now);

  LinkState lastState_ = LinkState::Offline;
  uint32_t lastActivity_ = 0;
  bool dimmed_ = false, screenOff_ = false, manualOffline_ = false;
  uint32_t nextReconnect_ = 0, backoff_ = 2000;
  uint32_t ledFlashUntil_ = 0, termAt_ = 0;
  uint16_t ledFlashColor_ = 0;
};

App& app();
