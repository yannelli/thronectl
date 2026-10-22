#include <cstdio>
#include <M5Cardputer.h>
#include <esp32-hal-rmt.h>
#include "hal/led.h"

M5Stub M5;
static int failures = 0, writes = 0;
static bool initFails = true, writeFails = false;
static float tickNs = 0;
static std::vector<rmt_data_t> sent;

static void check(bool ok, const char* message) {
  printf("%s %s\n", ok ? "ok  " : "FAIL", message);
  if (!ok) ++failures;
}

rmt_obj_t* rmtInit(int pin, bool tx, int blocks) {
  static rmt_obj_t output;
  check(pin == 21 && tx && blocks == 1, "GPIO 21 uses one transmit memory block");
  return initFails ? nullptr : &output;
}
float rmtSetTick(rmt_obj_t*, float tick) { return tickNs = tick; }
bool rmtWriteBlocking(rmt_obj_t*, rmt_data_t* frame, size_t count) {
  ++writes;
  sent.assign(frame, frame + count);
  return !writeFails;
}

static uint32_t decodedColor() {
  uint32_t grb = 0;
  bool timing = sent.size() == 26 && tickNs > 0;
  if (!timing) return 0;
  for (size_t i = 1; i < 25; ++i) {
    const auto& bit = sent[i];
    float high = bit.duration0 * tickNs, low = bit.duration1 * tickNs;
    bool one = high >= 580;
    timing &= bit.level0 == 1 && bit.level1 == 0;
    timing &= one ? high <= 1000 && low >= 220 && low <= 420
                  : high >= 220 && high <= 380 && low >= 580 && low <= 1000;
    grb = (grb << 1) | one;
  }
  for (size_t i : {size_t(0), size_t(25)}) {
    timing &= sent[i].level0 == 0 && sent[i].level1 == 0;
    timing &= (sent[i].duration0 + sent[i].duration1) * tickNs > 280000;
  }
  check(timing, "data pulses and reset gaps meet the WS2812B-2020 timing limits");
  return grb;
}

int main() {
  LedOutput led;
  led.setBrightness(64);
  led.setColor({255, 0, 0});
  check(writes == 0, "no data sent into a switched supply");
  led.setPower(true);
  check(M5.Display.brightness.back() == 255, "an effect holds the shared supply on");
  led.setColor({0x12, 0x34, 0x56});
  check(writes == 0, "initialization failure leaves the color pending");
  initFails = false;
  led.setColor({0x12, 0x34, 0x56});
  check(writes == 1 && decodedColor() == 0x341256, "the retry sends GRB most significant bit first");
  led.setColor({0x12, 0x34, 0x56});
  check(writes == 1, "unchanged colors do not send extra frames");
  size_t levels = M5.Display.brightness.size();
  led.setBrightness(8);
  led.setColor({});
  check(M5.Display.brightness.size() == levels, "dimming and a dark effect phase keep power steady");
  led.setColor({0x12, 0x34, 0x56});
  led.invalidate();
  led.setBrightness(8);
  int before = writes;
  led.setColor({0x12, 0x34, 0x56});
  check(writes == before + 1 && M5.Display.brightness.back() == 255,
        "panel sleep or wake restores power and resends the latched color");
  led.setPower(false);
  check(decodedColor() == 0 && M5.Display.brightness.back() == 8,
        "ending an effect clears the LED and restores the latest brightness");
  led.setBrightness(0);
  before = writes;
  led.setColor({255, 255, 255});
  check(writes == before && M5.Display.brightness.back() == 0, "screen off cuts power when LED is disabled");
  led.setPower(true);
  writeFails = true;
  led.setColor({255, 255, 255});
  before = writes;
  writeFails = false;
  led.setColor({255, 255, 255});
  check(writes == before + 1 && decodedColor() == 0xffffff, "failed writes are retried");
  printf("ledcheck: %d failures\n", failures);
  return failures ? 1 : 0;
}
