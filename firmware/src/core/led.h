#pragma once
#include <array>
#include "platform.h"

/** One LED behavior: an effect with its color, speed, level and, for alerts, how long it runs. */
struct LedSlot {
  uint8_t fx = 0, color = 0, speed = 1, level = 4, length = 2;
};

// Background states first, then the alerts. Settings keeps one LedSlot for each.
enum LedSlotId : uint8_t {
  LED_STANDBY, LED_WORKING, LED_WAITING, LED_OFFLINE, LED_CHARGING,
  LED_DONE, LED_ATTN, LED_ERROR, LED_INFO, LED_SLOTS
};
constexpr int LED_FIRST_ALERT = LED_DONE;

struct Rgb {
  uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

namespace led {

extern const char* const SLOT_NAMES[LED_SLOTS];
extern const char* const SLOT_HELP[LED_SLOTS];
extern const char* const FX_NAMES[];
extern const char* const COLOR_NAMES[];
extern const char* const SPEED_NAMES[];
extern const char* const LEVEL_NAMES[];
extern const char* const LENGTH_NAMES[];
extern const int FX_COUNT, COLOR_COUNT, SPEED_COUNT, LEVEL_COUNT, LENGTH_COUNT;

/** Factory behavior for a slot, and for all of them. */
LedSlot defaults(int slot);
std::array<LedSlot, LED_SLOTS> allDefaults();
/** Color the "Theme accent" choice uses for a slot. */
uint16_t themeColor(int slot);
/** Milliseconds one cycle of the slot's effect takes. */
uint32_t periodMs(const LedSlot& s);
/** How long an alert runs, 0 for "until a key press". */
uint32_t lengthMs(const LedSlot& s);
/** The LED color `t` ms into the effect, scaled by `bright` (0 to 255). */
Rgb render(const LedSlot& s, int slot, uint32_t t, uint8_t bright);

}  // namespace led
