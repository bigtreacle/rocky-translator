// Eridian Translator firmware.
//
// M2 state: the translator screen is live, driven by the brain's state machine.
// Chords are timed by a stub (no sound until M1); the mic and IMU events are
// simulated over serial until M4/M5:
//   tap            same as touching the screen
//   speech         a 1.5 s burst of "speech", then a reply
//   whistle <midi> a whistled note (e.g. whistle 72 = C5), then the mirror reply
//   pickup | shake IMU gestures
//   shift          burn-in nudge now        b <0-100>  brightness
//   m0             the M0 hardware test screen (reset to come back)
#include <Arduino.h>
#include <LittleFS.h>

#include "brain/brain.h"
#include "phrases/phrase_bank.h"
#include "hal/board.h"
#include "ui/bringup_screen.h"
#include "ui/lvgl_port.h"
#include "ui/translator_screen.h"
#include "version.h"

static hal::BoardInfo g_info;
static bool g_powerOk = false;
static uint32_t g_speechEndAt = 0;

static void onTap() { brain::onTap(); }

static void handleSerial() {
  static char line[40];
  static size_t n = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      line[n] = 0;
      n = 0;
      if (!line[0]) continue;
      char cmd[16] = {0};
      int arg = 0;
      sscanf(line, "%15s %d", cmd, &arg);
      if (!strcmp(cmd, "tap")) brain::onTap();
      else if (!strcmp(cmd, "speech")) { brain::onSpeechStart(); g_speechEndAt = millis() + 1500; }
      else if (!strcmp(cmd, "whistle")) brain::onWhistle(arg ? arg : 72, (int)random(-30, 31));
      else if (!strcmp(cmd, "pickup")) brain::onPickedUp();
      else if (!strcmp(cmd, "shake")) brain::onShaken();
      else if (!strcmp(cmd, "shift")) ui::translator::shiftForBurnIn();
      else if (!strcmp(cmd, "b")) hal::display().setBrightness((uint8_t)arg);
      else if (!strcmp(cmd, "m0")) { ui::bringupCreate("ERIDIAN M0", g_info.revision); }
      else if (!strcmp(cmd, "reload")) Serial.printf("phrases: %d (%s)\n", phrases::reload(), phrases::source());
      else if (!strcmp(cmd, "phrases")) {
        for (int i = 0; i < phrases::count(); i++) {
          const phrases::Phrase *p = phrases::at(i);
          Serial.printf("  %-10s trig %d chords %d next %-8s %s\n", p->id.c_str(), (int)p->trigger,
                        (int)p->chords.size(), p->next.empty() ? "-" : p->next.c_str(), p->text.c_str());
        }
      }
      else Serial.println("commands: tap, speech, whistle <midi>, pickup, shake, shift, b <0-100>, reload, phrases, m0");
    } else if (n < sizeof line - 1) {
      line[n++] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block on USB CDC when no host is reading
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) delay(10);

  Serial.printf("\nEridian Translator %s (%s)\n", FIRMWARE_VERSION, BOARD_VARIANT);
  bool boardOk = hal::boardBegin(g_info);
  Serial.printf("board: %s, %s, I2C SDA %d SCL %d, %d devices\n", g_info.name, g_info.revision, g_info.sda,
                g_info.scl, g_info.i2cFoundCount);
  if (!boardOk) Serial.println("board: bring-up failed, check I2C");

  bool displayOk = hal::display().begin();
  bool touchOk = hal::touch().begin();
  hal::motion().begin();
  g_powerOk = hal::power().begin();
  hal::audioOut().begin(22050);  // stub until M1; drives the amp enable pin
  Serial.printf("display %s, touch %s (%s), power %s\n", displayOk ? "ok" : "FAILED", touchOk ? "ok" : "FAILED",
                hal::touch().chipName(), g_powerOk ? "ok" : "FAILED");

  if (displayOk && ui::lvglBegin(hal::display(), hal::touch())) {
    ui::translator::create();
    ui::translator::setTapHandler(onTap);
  }
  if (!LittleFS.begin(false)) Serial.println("littlefs: mount failed (run: pio run -t uploadfs)");
  phrases::begin();
  brain::begin();
  Serial.printf("ready, %d phrases (%s)\n", phrases::count(), phrases::source());
}

void loop() {
  uint32_t now = millis();
  handleSerial();
  if (g_speechEndAt && now >= g_speechEndAt) {
    g_speechEndAt = 0;
    brain::onSpeechEnd();
  }

  static uint32_t lastPower = 0;
  if (g_powerOk && now - lastPower >= 5000) {
    lastPower = now;
    hal::PowerState ps = hal::power().read();
    brain::setBattery(ps.batteryPercent, ps.charging || ps.usbPresent);
  }

  static uint32_t lastFrame = 0;
  if (now - lastFrame >= 33) {  // ~30 fps model updates; LVGL redraws what changed
    lastFrame = now;
    brain::tick(now);
    ui::translator::apply(brain::model());
  }
  ui::lvglLoop();

  static const char *lastState = "";
  if (brain::stateName() != lastState) {
    lastState = brain::stateName();
    Serial.printf("state: %s\n", lastState);
  }
  delay(2);
}
