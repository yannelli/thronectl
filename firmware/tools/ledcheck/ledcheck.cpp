#include <cstdio>
#include <M5Cardputer.h>
#include <esp32-hal-rmt.h>
#include "hal/led.h"
#include "core/theme.h"

const Theme& T() {
  static const Theme theme{"test", 0, 0, 0, 0, 0, 0xf800, 0, 0, 0, 0, 0, 0};
  return theme;
}

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

static int named(const char* name, const char* const* names, int count) {
  for (int i = 0; i < count; ++i) if (std::strcmp(name, names[i]) == 0) return i;
  check(false, name);
  return 0;
}

static void checkEffects(LedOutput& output) {
  const char* oldColors[] = {"Default", "Theme accent", "Red", "Orange", "Amber", "Yellow", "Green",
                            "Teal", "Cyan", "Blue", "Indigo", "Purple", "Pink", "White"};
  const char* oldEffects[] = {"Off", "Solid", "Breathe", "Pulse", "Blink", "Double blink",
                             "Heartbeat", "Strobe", "Flicker", "Rainbow", "Ramp"};
  bool compatible = true;
  for (int i = 0; i < 14; ++i) compatible &= std::strcmp(oldColors[i], led::COLOR_NAMES[i]) == 0;
  for (int i = 0; i < 11; ++i) compatible &= std::strcmp(oldEffects[i], led::FX_NAMES[i]) == 0;
  check(compatible, "saved color and effect indices retain their names");

  struct ColorCase { const char* name; Rgb rgb; uint32_t grb; };
  const ColorCase colors[] = {
    {"Red", {255, 0, 0}, 0x00ff00}, {"Green", {0, 255, 0}, 0xff0000},
    {"Blue", {0, 0, 255}, 0x0000ff}, {"Yellow", {255, 255, 0}, 0xffff00},
    {"Cyan", {0, 255, 255}, 0xff00ff}, {"White", {255, 255, 255}, 0xffffff},
    {"Magenta", {255, 0, 255}, 0x00ffff}, {"Theme accent", {255, 0, 0}, 0x00ff00}
  };
  LedSlot slot{1, 0, 1, 4, 1};
  for (const auto& color : colors) {
    slot.color = named(color.name, led::COLOR_NAMES, led::COLOR_COUNT);
    Rgb rendered = led::render(slot, LED_STANDBY, 0, 255);
    output.setColor(rendered);
    check(rendered == color.rgb && decodedColor() == color.grb, color.name);
  }
  auto at = [&slot](const char* name) {
    slot.color = named(name, led::COLOR_NAMES, led::COLOR_COUNT);
    return led::render(slot, LED_STANDBY, 0, 255);
  };
  Rgb orange = at("Orange"), amber = at("Amber");
  check(orange.r == 255 && orange.g > 60 && orange.g < 110 && amber.g > orange.g + 30 && amber.g < 180,
        "Orange and Amber are gamma corrected, not yellow");
  bool distinct = true;
  for (int i = 2; i < led::COLOR_COUNT; ++i)
    for (int j = i + 1; j < led::COLOR_COUNT; ++j) {
      slot.color = i;
      Rgb a = led::render(slot, LED_STANDBY, 0, 255);
      slot.color = j;
      Rgb b = led::render(slot, LED_STANDBY, 0, 255);
      int d = std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
      if (d < 40) {
        printf("  too close: %s and %s\n", led::COLOR_NAMES[i], led::COLOR_NAMES[j]);
        distinct = false;
      }
    }
  check(distinct, "every named color looks different on the LED");
  const char* addedColors[] = {"Lime", "Mint", "Sky blue", "Violet", "Magenta", "Coral", "Warm white", "Ice white"};
  for (const char* name : addedColors) {
    slot.color = named(name, led::COLOR_NAMES, led::COLOR_COUNT);
    check(!(led::render(slot, LED_STANDBY, 0, 255) == Rgb{}), name);
  }
  const char* addedEffects[] = {"Rainbow breath", "Color steps", "Sparkle", "Candle", "Triple blink", "Swell"};
  slot.color = named("Blue", led::COLOR_NAMES, led::COLOR_COUNT);
  for (const char* name : addedEffects) {
    slot.fx = named(name, led::FX_NAMES, led::FX_COUNT);
    slot.speed = 1;
    uint32_t period = led::periodMs(slot);
    Rgb first = led::render(slot, LED_DONE, 0, 128);
    bool varied = false, valid = true;
    for (uint32_t t = 0; t < period * 3; t += 7) {
      Rgb c = led::render(slot, LED_DONE, t, 128);
      varied |= !(c == first);
      valid &= c == led::render(slot, LED_DONE, t, 128);
      valid &= c.r <= 128 && c.g <= 128 && c.b <= 128;
      valid &= led::render(slot, LED_DONE, t, 0) == Rgb{};
      if (slot.fx >= 13) valid &= c.r == 0 && c.g == 0;
    }
    valid &= led::lengthMs(slot) == period * 2;
    slot.speed = 0;
    valid &= led::periodMs(slot) > period;
    slot.speed = 3;
    valid &= led::periodMs(slot) < period;
    check(varied && valid, name);
  }
  slot.fx = named("Color steps", led::FX_NAMES, led::FX_COUNT);
  slot.speed = 1;
  const Rgb steps[] = {{255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255}, {0, 0, 255}, {255, 0, 255}};
  bool stepped = true;
  for (uint32_t i = 0; i < 6; ++i)
    stepped &= led::render(slot, LED_DONE, i * led::periodMs(slot) / 6, 255) == steps[i];
  check(stepped, "Color steps visits six distinct colors");
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
  checkEffects(led);
  printf("ledcheck: %d failures\n", failures);
  return failures ? 1 : 0;
}
