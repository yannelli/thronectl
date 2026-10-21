#pragma once
#include "services.h"

enum class UpdatePhase : uint8_t { Idle, Running, Done, Failed };

/** One firmware update: chunks from the host link or an SD image, written to flash or to the SD card. */
class Updater {
 public:
  void reset();
  /** Reads the image from the host with ota.read. toSd saves it for Launcher instead of flashing it. */
  void startLink(const std::string& version, uint32_t size, const std::string& sha256, bool toSd);
  void startSd(const std::string& path);
  /** Body of a KIND_OTA frame: u32 big-endian offset, then the bytes. */
  void onData(const uint8_t* data, size_t len);
  void update(uint32_t now);
  void cancel(const std::string& why);
  /** Tells the host the result of an offer, for offers that never start a job. */
  static void reply(bool ok, const std::string& err, const char* dest);

  UpdatePhase phase() const { return phase_; }
  bool active() const { return phase_ == UpdatePhase::Running; }
  bool fromLink() const { return fromLink_; }
  bool toSd() const { return toSd_; }
  uint32_t written() const { return off_; }
  uint32_t size() const { return size_; }
  const std::string& version() const { return version_; }
  const std::string& error() const { return err_; }
  /** SD file written for Launcher, or the SD file being installed. */
  const std::string& sdPath() const { return sdPath_; }
  /** True once the new image is set to boot; the app restarts shortly after. */
  bool restartDue(uint32_t now) const { return rebootAt_ && (int32_t)(now - rebootAt_) >= 0; }

  static constexpr uint32_t CHUNK = 8192;

 private:
  void request();
  void fail(const std::string& why);
  void complete();

  UpdatePhase phase_ = UpdatePhase::Idle;
  bool fromLink_ = false, toSd_ = false;
  uint32_t size_ = 0, off_ = 0, asked_ = 0, rebootAt_ = 0;
  int inflight_ = 0;
  uint32_t gen_ = 0;
  std::string version_, err_, sdPath_;
  std::vector<uint8_t> buf_;
};

Updater& updater();
