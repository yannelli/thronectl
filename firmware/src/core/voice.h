#pragma once
#include "platform.h"
#include "input.h"

/** Push-to-talk: records on G0, streams to the host, delivers the transcript to the top screen. */
class Voice {
 public:
  enum class St : uint8_t { Idle, Rec, Wait, Review };

  void pttDown();
  void pttUp();
  /** Space key: starts recording, or stops and sends it. */
  void toggle();
  void update(uint32_t now);
  bool onKey(const KeyEvent& e);
  bool active() const { return st_ != St::Idle; }
  void cancel();

 private:
  void start();
  void stopRecording();
  void deliver(const std::string& text);
  void show();
  void hide();
  void draw(Canvas& c);

  St st_ = St::Idle;
  bool byKey_ = false;  // started with the space key, so space or ok stops it
  uint32_t t0_ = 0, samples_ = 0;
  std::string text_, target_;
  float level_ = 0;
};
