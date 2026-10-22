#include "led.h"
#include <cmath>
#include "theme.h"

namespace led {

const char* const SLOT_NAMES[LED_SLOTS] = {"Standby", "Working", "Waiting", "Offline", "Charging",
                                           "Done", "Attention", "Error", "Info"};
const char* const SLOT_HELP[LED_SLOTS] = {
    "connected, no agent busy", "an agent is running", "an agent needs you", "no host link",
    "on USB power, nothing else to show", "an agent finished a turn", "approval or input needed",
    "an agent or command failed", "other notices"};
const char* const FX_NAMES[] = {"Off", "Solid", "Breathe", "Pulse", "Blink", "Double blink",
                                "Heartbeat", "Strobe", "Flicker", "Rainbow", "Ramp"};
const char* const COLOR_NAMES[] = {"Default", "Theme accent", "Red", "Orange", "Amber", "Yellow", "Green",
                                   "Teal", "Cyan", "Blue", "Indigo", "Purple", "Pink", "White"};
const char* const SPEED_NAMES[] = {"Slow", "Normal", "Fast", "Very fast"};
const char* const LEVEL_NAMES[] = {"10%", "25%", "50%", "75%", "100%"};
const char* const LENGTH_NAMES[] = {"1 cycle", "2 cycles", "3 cycles", "5 cycles", "3 s",
                                    "10 s", "30 s", "1 min", "Until a key press"};
#define COUNT(a) (int)(sizeof(a) / sizeof((a)[0]))
const int FX_COUNT = COUNT(FX_NAMES), COLOR_COUNT = COUNT(COLOR_NAMES), SPEED_COUNT = COUNT(SPEED_NAMES),
          LEVEL_COUNT = COUNT(LEVEL_NAMES), LENGTH_COUNT = COUNT(LENGTH_NAMES);

namespace {

enum Fx : uint8_t { OFF, SOLID, BREATHE, PULSE, BLINK, DOUBLE, HEART, STROBE, FLICKER, RAINBOW, RAMP };

const uint32_t PALETTE[] = {0, 0, 0xFF1A1A, 0xFF6A00, 0xFFA800, 0xFFE000, 0x20FF40,
                            0x00E0A0, 0x00D8FF, 0x2060FF, 0x5030FF, 0xB030FF, 0xFF30A0, 0xFFFFFF};
// Per slot: the color "Default" means, as an index into PALETTE (1 = theme accent).
const uint8_t SLOT_COLOR[LED_SLOTS] = {1, 8, 4, 2, 6, 6, 4, 2, 9};
const uint16_t FX_PERIOD[] = {1000, 1000, 3200, 1600, 1000, 1500, 1300, 400, 1000, 5000, 1400};
const float SPEED_K[] = {1.8f, 1.0f, 0.55f, 0.3f};
const uint8_t LEVELS[] = {26, 64, 128, 191, 255};
const uint8_t CYCLES[] = {1, 2, 3, 5};
const uint32_t LENGTH_MS[] = {3000, 10000, 30000, 60000};

const LedSlot DEFAULTS[LED_SLOTS] = {
    {BREATHE, 0, 0, 0, 0},   // standby: slow dim breathing
    {BREATHE, 0, 1, 3, 0},   // working
    {PULSE, 0, 1, 4, 0},     // waiting
    {OFF, 0, 1, 2, 0},       // offline
    {OFF, 0, 0, 1, 0},       // charging
    {DOUBLE, 0, 1, 4, 2},    // done: three double blinks
    {BLINK, 0, 2, 4, 5},     // attention: 10 s
    {STROBE, 0, 1, 4, 4},    // error: 3 s
    {PULSE, 0, 1, 2, 0},     // info: one pulse
};

constexpr float PI_F = 3.14159265f;

float bump(float p, float at, float w) { return p >= at && p < at + w ? sinf(PI_F * (p - at) / w) : 0; }

float noise(uint32_t n) {
  n = n * 1103515245u + 12345u;
  n ^= n >> 13;
  n *= 0x5bd1e995u;
  return (float)((n >> 8) & 0xFFFF) / 65535.0f;
}

float shape(uint8_t fx, float p, uint32_t t) {
  switch (fx) {
    case SOLID: case RAINBOW: return 1;
    case BREATHE: return 0.04f + 0.96f * (0.5f - 0.5f * cosf(2 * PI_F * p));
    case PULSE: return p < 0.12f ? p / 0.12f : expf(-(p - 0.12f) * 5.0f);
    case BLINK: return p < 0.5f ? 1 : 0;
    case DOUBLE: return p < 0.12f || (p >= 0.25f && p < 0.37f) ? 1 : 0;
    case HEART: return fmaxf(bump(p, 0, 0.14f), 0.7f * bump(p, 0.22f, 0.14f));
    case STROBE: return p < 0.12f ? 1 : 0;
    case FLICKER: {
      uint32_t step = t / 70;
      float f = (float)(t % 70) / 70.0f;
      return 0.35f + 0.65f * (noise(step) * (1 - f) + noise(step + 1) * f);
    }
    case RAMP: return p;
    default: return 0;
  }
}

Rgb fromHex(uint32_t c) { return {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c}; }

Rgb from565(uint16_t c) {
  uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  return {(uint8_t)(r << 3 | r >> 2), (uint8_t)(g << 2 | g >> 4), (uint8_t)(b << 3 | b >> 2)};
}

Rgb hue(float p) {
  float h = p * 6;
  int i = (int)h % 6;
  float f = h - floorf(h);
  uint8_t up = (uint8_t)(255 * f), down = (uint8_t)(255 * (1 - f));
  switch (i) {
    case 0: return {255, up, 0};
    case 1: return {down, 255, 0};
    case 2: return {0, 255, up};
    case 3: return {0, down, 255};
    case 4: return {up, 0, 255};
    default: return {255, 0, down};
  }
}

}  // namespace

LedSlot defaults(int slot) { return slot >= 0 && slot < LED_SLOTS ? DEFAULTS[slot] : LedSlot{}; }

std::array<LedSlot, LED_SLOTS> allDefaults() {
  std::array<LedSlot, LED_SLOTS> a;
  for (int i = 0; i < LED_SLOTS; i++) a[i] = DEFAULTS[i];
  return a;
}

uint16_t themeColor(int) { return T().accent; }

uint32_t periodMs(const LedSlot& s) {
  return (uint32_t)(FX_PERIOD[s.fx % FX_COUNT] * SPEED_K[s.speed % SPEED_COUNT]);
}

uint32_t lengthMs(const LedSlot& s) {
  int n = s.length % LENGTH_COUNT;
  if (n < 4) return periodMs(s) * CYCLES[n];
  return n < 8 ? LENGTH_MS[n - 4] : 0;
}

Rgb render(const LedSlot& s, int slot, uint32_t t, uint8_t bright) {
  uint8_t fx = s.fx % FX_COUNT;
  if (fx == OFF || !bright || slot < 0 || slot >= LED_SLOTS) return {};
  uint32_t per = periodMs(s);
  float p = (float)(t % per) / per;
  float v = shape(fx, p, t);
  int ci = s.color % COLOR_COUNT;
  if (!ci) ci = SLOT_COLOR[slot];
  Rgb c = fx == RAINBOW ? hue(p) : ci == 1 ? from565(themeColor(slot)) : fromHex(PALETTE[ci]);
  // Squaring the shape evens out how the eye sees the fade; the level and brightness scale it linearly.
  float k = v * v * LEVELS[s.level % LEVEL_COUNT] / 255.0f * bright / 255.0f;
  auto ch = [k](uint8_t x) { return (uint8_t)(x * k + 0.5f); };
  return {ch(c.r), ch(c.g), ch(c.b)};
}

}  // namespace led
