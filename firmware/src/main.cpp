#include <M5Cardputer.h>
#include "core/app.h"
#include "core/settings.h"
#include "core/ui.h"
#include "hal/hal.h"
#include "net/crypto.h"
#include "net/link.h"
#include "net/wifi.h"

static KeyboardHal keyboard;
static AudioHal audio;
static SystemHal sys;
static NvsStore nvs;
static OtaHal ota;
static Net net;
static Link hostLink;
static Canvas* canvas = nullptr;
static uint32_t lastFrame = 0;

static std::string deviceName() {
  if (!settings().deviceName.empty()) return settings().deviceName;
  std::string id = sys.deviceId();
  return "Cardputer-" + id.substr(id.size() > 4 ? id.size() - 4 : 0);
}

static void loadKeys(uint8_t priv[32], uint8_t pub[32]) {
  if (nvs.loadBlob("sk", priv, 32) && tcrypto::publicKey(priv, pub)) return;
  tcrypto::keypair(priv, pub);
  nvs.saveBlob("sk", priv, 32);
}

void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = true;
  cfg.internal_spk = true;
  M5Cardputer.begin(cfg, true);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(0);
  settingsBind(&nvs);
  settingsLoad();

  canvas = new Canvas(&M5.Display);
  canvas->setColorDepth(16);
  canvas->createSprite(SCREEN_W, SCREEN_H);

  uint8_t priv[32], pub[32];
  loadKeys(priv, pub);
  hostLink.begin(priv, pub);
  memset(priv, 0, sizeof priv);
  hostLink.deviceName = deviceName;
  hostLink.onEvent = [](const char* ev, JsonVariantConst d) { app().onLinkEvent(ev, d); };
  hostLink.onSpeaker = [](const uint8_t* pcm, size_t n) { app().onSpeaker(pcm, n); };
  hostLink.onOta = [](const uint8_t* data, size_t n) { app().onOtaData(data, n); };

  sys.begin();
  audio.begin();
  net.onConnected = [] { sys.startNtp(); };
  net.begin(deviceName());

  Services& s = svc();
  s.link = &hostLink;
  s.net = &net;
  s.audio = &audio;
  s.sys = &sys;
  s.ota = &ota;
  app().begin();
}

void loop() {
  uint32_t now = millis();
  M5Cardputer.update();
  keyboard.poll(now, [](const KeyEvent& e) { app().onKey(e); });
  if (M5Cardputer.BtnA.wasPressed()) app().onPtt(true);
  if (M5Cardputer.BtnA.wasReleased()) app().onPtt(false);
  net.loop(now);
  hostLink.loop(now);
  audio.loop(now);
  sys.loop(now);
  app().loop(now);
  ui().tick(now);
  if (app().screenOn() && now - lastFrame >= app().frameInterval(now)) {
    lastFrame = now;
    ui().render(*canvas);
    canvas->pushSprite(0, 0);
  }
  delay(1);
}
