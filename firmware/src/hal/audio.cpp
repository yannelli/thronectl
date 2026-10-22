#include <M5Cardputer.h>
#include <cmath>
#include "../core/settings.h"
#include "hal.h"

static constexpr uint8_t CH_STREAM = 0, CH_CHIME = 1, CH_CLICK = 2;
static constexpr uint8_t ES8311_ADDR = 0x18, ES8311_PGA = 0x14, PGA_21DB = 0x17;  // MIC1 input, +21 dB

void AudioHal::begin() {
  micBuf_ = (int16_t*)malloc(sizeof(int16_t) * MIC_CHUNK * MIC_BUFS);
  ring_ = (int16_t*)malloc(sizeof(int16_t) * RING);
  out_ = (int16_t*)malloc(sizeof(int16_t) * OUT_CHUNK * OUT_BUFS);
  M5.Speaker.begin();
}

void AudioHal::setVolume(uint8_t v) { M5.Speaker.setVolume(v); }

void AudioHal::setMix(uint8_t alerts, uint8_t voice) {
  M5.Speaker.setChannelVolume(CH_CHIME, alerts);
  M5.Speaker.setChannelVolume(CH_STREAM, voice);
}

void AudioHal::click() {
  uint8_t mode = settings().keyClick;
  if (!mode || mic_ || streaming_) return;
  M5.Speaker.setChannelVolume(CH_CLICK, mode == 1 ? 70 : 200);
  M5.Speaker.tone(2600, 5, CH_CLICK, true);
}

void AudioHal::chime(Chime kind) {
  if (mic_) return;
  static const std::vector<Note> seqs[] = {
      {{523, 70}, {659, 70}, {784, 70}, {1047, 150}},
      {{784, 70}, {1047, 120}},
      {{880, 80}, {0, 50}, {880, 80}, {0, 50}, {1175, 140}},
      {{330, 120}, {262, 220}},
      {{1047, 60}},
      {{659, 60}, {988, 90}},
      {{440, 90}, {349, 150}},
      {{1200, 40}},
      {{900, 40}},
  };
  notes_ = seqs[kind];
  noteIdx_ = 0;
  noteAt_ = millis();
}

bool AudioHal::micStart(uint8_t gain) {
  if (mic_) return true;
  if (!micBuf_) return false;
  speakerStop();
  notes_.clear();
  M5.Speaker.end();
  auto cfg = M5.Mic.config();
  cfg.sample_rate = 16000;
  // M5Unified leaves the ES8311 mic PGA at 0 dB. Gain before the ADC is cleaner than digital gain,
  // so the codec gets +21 dB (about 11x) and the digital gain shrinks to match.
  cfg.magnification = gain / 11 ? gain / 11 : 1;
  cfg.noise_filter_level = 64;
  M5.Mic.config(cfg);
  bool ok = M5.Mic.begin();
  if (ok) M5.In_I2C.writeRegister8(ES8311_ADDR, ES8311_PGA, PGA_21DB, 400000);
  if (ok && M5.In_I2C.readRegister8(ES8311_ADDR, ES8311_PGA, 400000) != PGA_21DB) {
    M5.Mic.end();  // the codec did not take it: fall back to digital gain alone
    cfg.magnification = gain;
    M5.Mic.config(cfg);
    ok = M5.Mic.begin();
  }
  if (!ok) {
    M5.Speaker.begin();
    return false;
  }
  micHead_ = 0;
  micQueued_ = 0;
  for (int i = 0; i < 2; i++) {
    M5.Mic.record(micBuf_ + i * MIC_CHUNK, MIC_CHUNK, 16000);
    micQueued_++;
  }
  level_ = 0;
  mic_ = true;
  return true;
}

void AudioHal::micStop() {
  if (!mic_) return;
  while (M5.Mic.isRecording()) delay(1);
  M5.Mic.end();
  M5.Speaker.begin();
  mic_ = false;
  level_ = 0;
}

void AudioHal::micPoll(const std::function<void(const int16_t*, size_t)>& sink) {
  if (!mic_) return;
  while (micQueued_ > (int)M5.Mic.isRecording()) {
    const int16_t* buf = micBuf_ + micHead_ * MIC_CHUNK;
    double sum = 0;
    for (int i = 0; i < MIC_CHUNK; i++) sum += (double)buf[i] * buf[i];
    float rms = sqrtf(sum / MIC_CHUNK) / 5000.0f;
    level_ = rms > 1 ? 1 : sqrtf(rms);
    sink(buf, MIC_CHUNK);
    micHead_ = (micHead_ + 1) % MIC_BUFS;
    micQueued_--;
    int next = (micHead_ + micQueued_) % MIC_BUFS;
    M5.Mic.record(micBuf_ + next * MIC_CHUNK, MIC_CHUNK, 16000);
    micQueued_++;
  }
}

void AudioHal::speakerBegin(uint32_t rate) {
  if (mic_ || !ring_) return;
  M5.Speaker.stop(CH_STREAM);
  rate_ = rate ? rate : 16000;
  rHead_ = rLen_ = 0;
  streaming_ = true;
  ended_ = primed_ = false;
}

void AudioHal::speakerFeed(const uint8_t* pcm, size_t bytes) {
  if (!streaming_) return;
  const int16_t* s = (const int16_t*)pcm;
  size_t n = bytes / 2;
  for (size_t i = 0; i < n && rLen_ < RING; i++) {
    ring_[(rHead_ + rLen_) % RING] = s[i];
    rLen_++;
  }
}

void AudioHal::speakerEnd() { ended_ = true; }

void AudioHal::speakerStop() {
  if (!streaming_) return;
  M5.Speaker.stop(CH_STREAM);
  streaming_ = false;
  rHead_ = rLen_ = 0;
}

void AudioHal::pumpStream() {
  if (!primed_ && (rLen_ >= rate_ * 3 / 10 || ended_)) primed_ = true;
  if (!primed_) return;
  while (rLen_ > 0 && M5.Speaker.isPlaying(CH_STREAM) < 2) {
    size_t n = rLen_ < OUT_CHUNK ? rLen_ : OUT_CHUNK;
    if (n < OUT_CHUNK && !ended_) break;
    int16_t* buf = out_ + outIdx_ * OUT_CHUNK;
    for (size_t i = 0; i < n; i++) buf[i] = ring_[(rHead_ + i) % RING];
    rHead_ = (rHead_ + n) % RING;
    rLen_ -= n;
    M5.Speaker.playRaw(buf, n, rate_, false, 1, CH_STREAM, false);
    outIdx_ = (outIdx_ + 1) % OUT_BUFS;
  }
  if (ended_ && rLen_ == 0 && !M5.Speaker.isPlaying(CH_STREAM)) streaming_ = false;
}

void AudioHal::loop(uint32_t now) {
  if (streaming_) pumpStream();
  if (noteIdx_ < notes_.size() && (int32_t)(now - noteAt_) >= 0) {
    const Note& n = notes_[noteIdx_++];
    if (n.hz) M5.Speaker.tone(n.hz, n.ms, CH_CHIME, true);
    noteAt_ = now + n.ms;
  }
}
