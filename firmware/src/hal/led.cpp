#include <M5Cardputer.h>
#include <esp32-hal-rmt.h>
#include "led.h"

static bool writeColor(Rgb color) {
  static rmt_obj_t* output = nullptr;
  if (!output) {
    output = rmtInit(21, RMT_TX_MODE, RMT_MEM_64);
    if (!output) return false;
    rmtSetTick(output, 50);
  }
  // A complete GRB frame and both reset gaps fit in one ESP32-S3 RMT memory block.
  // The peripheral sends every edge without an interrupt-driven refill.
  rmt_data_t frame[26]{};
  frame[0].duration0 = frame[0].duration1 = 3000;
  frame[25] = frame[0];
  const uint8_t channels[] = {color.g, color.r, color.b};
  int n = 1;
  for (uint8_t channel : channels) {
    for (int bit = 7; bit >= 0; --bit) {
      bool high = channel & (1 << bit);
      frame[n].level0 = 1;
      frame[n].duration0 = high ? 18 : 6;
      frame[n].duration1 = high ? 7 : 19;
      ++n;
    }
  }
  return rmtWriteBlocking(output, frame, 26);
}

void LedOutput::setBrightness(uint8_t brightness) {
  brightness_ = brightness;
  applyBrightness();
}

void LedOutput::applyBrightness() {
  int brightness = powered_ ? 255 : brightness_;
  if (appliedBrightness_ == brightness) return;
  M5.Display.setBrightness(brightness);
  appliedBrightness_ = brightness;
  colorValid_ = false;
}

void LedOutput::invalidate() {
  appliedBrightness_ = -1;
  colorValid_ = false;
}

void LedOutput::setPower(bool powered) {
  if (powered_ == powered) return;
  if (!powered) setColor({});
  powered_ = powered;
  applyBrightness();
}

void LedOutput::setColor(Rgb color) {
  if (!powered_ || (colorValid_ && color == color_)) return;
  if (writeColor(color)) {
    color_ = color;
    colorValid_ = true;
  }
}
