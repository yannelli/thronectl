#pragma once
#include <cstddef>
#include <cstdint>

struct rmt_obj_t {};
struct rmt_data_t {
  uint32_t duration0 : 15;
  uint32_t level0 : 1;
  uint32_t duration1 : 15;
  uint32_t level1 : 1;
};
constexpr bool RMT_TX_MODE = true;
constexpr int RMT_MEM_64 = 1;
rmt_obj_t* rmtInit(int pin, bool tx, int blocks);
float rmtSetTick(rmt_obj_t* output, float tick);
bool rmtWriteBlocking(rmt_obj_t* output, rmt_data_t* frame, size_t count);
