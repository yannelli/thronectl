#pragma once
#include "../core/led.h"

class LedOutput {
 public:
  void setBrightness(uint8_t brightness);
  void setPower(bool powered);
  void setColor(Rgb color);
  void invalidate();

 private:
  void applyBrightness();
  uint8_t brightness_ = 0;
  int appliedBrightness_ = -1;
  bool powered_ = false, colorValid_ = false;
  Rgb color_{};
};
