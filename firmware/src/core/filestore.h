#pragma once
#include "settings.h"

/** Keeps each key in its own file in one folder of a mounted file system (LittleFS on the device). */
class FileStore : public SettingsStore {
 public:
  explicit FileStore(std::string dir) : dir_(std::move(dir)) {}
  std::string load(const char* key) override;
  void save(const char* key, const std::string& value) override;
  void wipe() override;

 private:
  std::string path(const char* key) const { return dir_ + "/" + key; }
  std::string dir_;
};
