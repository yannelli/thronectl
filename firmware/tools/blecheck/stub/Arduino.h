#pragma once
// Host stand-ins for the Arduino and FreeRTOS calls that src/hal/ble.cpp makes.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

extern uint32_t g_millis;
inline uint32_t millis() { return g_millis; }
struct EspStub {
  uint32_t freeHeap = 200 * 1024;
  uint32_t getFreeHeap() const { return freeHeap; }
};
extern EspStub ESP;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
