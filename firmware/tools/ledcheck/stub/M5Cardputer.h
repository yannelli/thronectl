#pragma once
#include <cstdint>
#include <vector>

struct DisplayStub {
  std::vector<uint8_t> brightness;
  void setBrightness(uint8_t value) { brightness.push_back(value); }
};
struct M5Stub { DisplayStub Display; };
extern M5Stub M5;
