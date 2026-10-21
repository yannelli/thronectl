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

using Canvas = lgfx::LGFX_Sprite;
using Font = lgfx::IFont;

static constexpr int SCREEN_W = 240;
static constexpr int SCREEN_H = 135;
static constexpr int STATUS_H = 12;
static constexpr int HINT_H = 10;

#define FW_VERSION "0.1.1"
#define THRONE_PORT 4787
