#include "voice.h"
#include <cmath>
#include "../screens/screens.h"
#include "gfx.h"
#include "services.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

static constexpr uint32_t MAX_REC_MS = 60000;
static constexpr int RATE = 16000;

void Voice::show() {
  ui().overlay = [this](Canvas& c) { draw(c); };
}

void Voice::hide() {
  ui().overlay = nullptr;
}

void Voice::pttDown() {
  Services& s = svc();
  if (st_ == St::Rec && settings().pttMode == 1) {
    stopRecording();
    return;
  }
  if (st_ != St::Idle) return;
  if (!s.link || !s.link->ready()) {
    ui().toast("error", "Not connected", "Voice needs a paired host");
    s.audio->chime(CH_FAIL);
    return;
  }
  Screen* top = ui().top();
  target_ = top ? top->voiceTarget() : "new agent";
  s.audio->speakerStop();
  JsonDocument p;
  p["rate"] = RATE;
  s.link->call("voice.start", p, [](bool, JsonVariantConst, const char*) {});
  if (!s.audio->micStart(MIC_GAIN_VALUES[settings().micGain % 4])) {
    ui().toast("error", "Microphone failed");
    s.link->call("voice.cancel", [](bool, JsonVariantConst, const char*) {});
    return;
  }
  st_ = St::Rec;
  t0_ = nowMs();
  samples_ = 0;
  text_.clear();
  show();
}

void Voice::pttUp() {
  if (st_ == St::Rec && settings().pttMode == 0) stopRecording();
}

void Voice::stopRecording() {
  Services& s = svc();
  s.audio->micPoll([&](const int16_t* pcm, size_t n) { s.link->sendMic(pcm, n); samples_ += n; });
  s.audio->micStop();
  if (samples_ < RATE * 0.35) {
    s.link->call("voice.cancel", [](bool, JsonVariantConst, const char*) {});
    st_ = St::Idle;
    hide();
    return;
  }
  st_ = St::Wait;
  JsonDocument p;
  p.to<JsonObject>();
  s.link->call("voice.stop", p, [this](bool ok, JsonVariantConst r, const char* err) {
    if (st_ != St::Wait) return;
    if (!ok) {
      ui().toast("error", "Transcription failed", err ? err : "");
      svc().audio->chime(CH_FAIL);
      st_ = St::Idle;
      hide();
      return;
    }
    text_ = r["text"] | "";
    if (text_.empty()) {
      ui().toast("info", "Didn't catch that", "Hold G0 and speak");
      st_ = St::Idle;
      hide();
      return;
    }
    svc().audio->chime(CH_OK);
    if (settings().autoSend) deliver(text_);
    else st_ = St::Review;
  }, 60000);
}

void Voice::deliver(const std::string& text) {
  st_ = St::Idle;
  hide();
  Screen* top = ui().top();
  if (!top || !top->onVoiceText(text)) ui().push(makeNewAgent(text));
}

void Voice::cancel() {
  Services& s = svc();
  if (st_ == St::Rec) {
    s.audio->micStop();
    s.link->call("voice.cancel", [](bool, JsonVariantConst, const char*) {});
  }
  st_ = St::Idle;
  hide();
}

void Voice::update(uint32_t now) {
  if (st_ != St::Rec) return;
  Services& s = svc();
  s.audio->micPoll([&](const int16_t* pcm, size_t n) { s.link->sendMic(pcm, n); samples_ += n; });
  level_ = level_ * 0.6f + s.audio->micLevel() * 0.4f;
  if (elapsedMs(now, t0_) > MAX_REC_MS || !s.link->ready()) stopRecording();
}

bool Voice::onKey(const KeyEvent& e) {
  if (st_ == St::Idle) return false;
  if (st_ == St::Review) {
    if (e.key == K_ENTER) deliver(text_);
    else if (e.key == 'e') {
      std::string t = text_;
      st_ = St::Idle;
      hide();
      ui().input("Edit transcript", t, true, [this](const std::string& v) { if (!v.empty()) deliver(v); });
    } else if (isBack(e)) cancel();
    return true;
  }
  if (isBack(e)) cancel();
  return true;
}

void Voice::draw(Canvas& c) {
  const Theme& t = T();
  int x = 16, y = 22, w = SCREEN_W - 32, h = 96;
  c.fillRoundRect(x, y, w, h, 8, t.panel);
  c.drawRoundRect(x, y, w, h, 8, st_ == St::Rec ? t.err : t.accent);
  uint32_t now = nowMs();
  if (st_ == St::Rec) {
    float pulse = 0.5f + 0.5f * sinf(now / 120.0f);
    int r = 13 + (int)(level_ * 10);
    c.fillCircle(x + 30, y + 34, r, blend(t.panel, t.err, (uint8_t)(90 + 80 * pulse)));
    gfx::mic(c, x + 30, y + 34, 18, t.fg);
    char secs[16];
    snprintf(secs, sizeof secs, "%.1fs", (elapsedMs(now, t0_)) / 1000.0f);
    gfx::text(c, F::bold, x + 56, y + 14, "Listening", t.fg);
    gfx::text(c, F::small, x + w - 8, y + 17, secs, t.dim, gfx::Datum::top_right);
    gfx::text(c, F::small, x + 56, y + 32, gfx::ellipsize(c, F::small, "to " + target_, w - 64), t.accent);
    for (int i = 0; i < 24; i++) {
      float v = level_ * (0.4f + 0.6f * fabsf(sinf(now / 90.0f + i * 0.7f)));
      int bh = 2 + (int)(v * 26);
      c.fillRoundRect(x + 56 + i * 6, y + 62 - bh / 2, 4, bh, 2, i % 3 ? t.accent : t.accent2);
    }
    gfx::text(c, F::small, x + w / 2, y + h - 12, settings().pttMode ? "press G0 to send" : "release G0 to send", t.dim, gfx::Datum::top_center);
  } else if (st_ == St::Wait) {
    gfx::spinner(c, x + 30, y + h / 2, 12, now, t.accent);
    gfx::text(c, F::bold, x + 56, y + h / 2 - 7, "Transcribing", t.fg);
  } else if (st_ == St::Review) {
    gfx::text(c, F::text, x + 8, y + 5, "Heard:", t.dim);
    auto lines = gfx::wrap(c, F::text, text_, w - 16, 5);
    for (size_t i = 0; i < lines.size(); i++) gfx::text(c, F::text, x + 8, y + 18 + (int)i * 11, lines[i], t.fg);
    gfx::text(c, F::small, x + w / 2, y + h - 12, "ent send  e edit  ` cancel", t.accent, gfx::Datum::top_center);
  }
}
