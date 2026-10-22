#pragma once
// Portable layer: UI code builds for the device and for the host snapshot tool.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>

#ifdef ARDUINO
#include <Arduino.h>
#include <M5GFX.h>
inline uint32_t nowMs() { return millis(); }
#else
#include <lgfx/v1/LGFX_Sprite.hpp>
#include <lgfx/v1/lgfx_fonts.hpp>
uint32_t nowMs();
#endif

/** Milliseconds from `since` to `now`, or 0 when `since` was stamped after `now` was read. */
inline uint32_t elapsedMs(uint32_t now, uint32_t since) {
  int32_t d = (int32_t)(now - since);
  return d > 0 ? (uint32_t)d : 0;
}

using Canvas = lgfx::LGFX_Sprite;
using Font = lgfx::IFont;

static constexpr int SCREEN_W = 240;
static constexpr int SCREEN_H = 135;
// Layout sizes. They grow with Settings > Display > Text size (applyTextSize in theme.cpp).
inline int STATUS_H = 12;
inline int HINT_H = 10;
inline int HEADER_H = 16;
inline int LINE_H = 11;  // one line of F::text
inline int ROW_PAD = 0;  // extra height added to every list row

#define FW_VERSION "0.4.0"
#define THRONE_PORT 4787
