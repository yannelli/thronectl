#pragma once
#include "platform.h"
#include <initializer_list>

struct Hint {
  const char* key;
  const char* label;
};

namespace gfx {
using Datum = lgfx::textdatum_t;

void text(Canvas& c, const Font* f, int x, int y, const std::string& s, uint16_t color, Datum d = Datum::top_left);
int width(Canvas& c, const Font* f, const std::string& s);
int height(Canvas& c, const Font* f);
std::string ellipsize(Canvas& c, const Font* f, const std::string& s, int maxW);
std::vector<std::string> wrap(Canvas& c, const Font* f, const std::string& s, int maxW, int maxLines = 99);

void bar(Canvas& c, int x, int y, int w, int h, float frac, uint16_t fg, uint16_t bg);
uint16_t loadColor(float frac);
void spark(Canvas& c, int x, int y, int w, int h, const float* hist, int len, int pos, int cap, float maxV, uint16_t color);
void wifi(Canvas& c, int x, int y, int rssi, bool connected, uint16_t color, uint16_t off);
void battery(Canvas& c, int x, int y, int pct, bool charging, uint16_t color, uint16_t outline);
void spinner(Canvas& c, int cx, int cy, int r, uint32_t t, uint16_t color);
void statusDot(Canvas& c, int cx, int cy, const std::string& status, uint32_t t);
void mic(Canvas& c, int cx, int cy, int h, uint16_t color);
int chip(Canvas& c, int x, int y, const std::string& s, uint16_t fg, uint16_t bg);
void hints(Canvas& c, std::initializer_list<Hint> items);
void header(Canvas& c, const std::string& title, const std::string& right = "");
void emptyState(Canvas& c, const char* title, const char* body);
}  // namespace gfx
