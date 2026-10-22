#include <esp_littlefs.h>
#include <esp_partition.h>
#include "hal.h"

// The default 8 MB table names the data partition "spiffs"; LittleFS uses it because it survives power cuts.
static const esp_partition_t* partition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
}

bool FlashStore::mount(bool mayFormat) {
  const esp_partition_t* p = partition();
  if (!p) return false;
  if (esp_littlefs_partition_mounted(p)) return true;
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = "/flash";
  conf.partition = p;
  conf.format_if_mount_failed = mayFormat;
  return esp_vfs_littlefs_register(&conf) == ESP_OK;
}

bool FlashStore::blank() {
  const esp_partition_t* p = partition();
  if (!p) return false;
  // SPIFFS and LittleFS both write their first blocks when they format, so 16 KB of erased flash is enough.
  uint32_t buf[64];
  for (size_t at = 0; at < 16384; at += sizeof buf) {
    if (esp_partition_read(p, at, buf, sizeof buf) != ESP_OK) return false;
    for (uint32_t w : buf)
      if (w != 0xFFFFFFFF) return false;
  }
  return true;
}

std::string FlashStore::usage() {
  const esp_partition_t* p = partition();
  size_t total = 0, used = 0;
  if (!p || !esp_littlefs_partition_mounted(p) || esp_littlefs_partition_info(p, &total, &used) != ESP_OK) return "";
  return std::to_string(used / 1024) + " of " + std::to_string(total / 1024) + " KB";
}
