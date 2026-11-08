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
//   mute           toggle the speaker (same as a short press of the BOOT button)
//   listen 0|1     microphone listening off/on   sens <6-24>  dB above the noise floor
//   mic            listening diagnostics (level, floor, pitch, flatness)
//   vol <0-100>    codec volume             audio      synth diagnostics
//   m0             the M0 hardware test screen (reset to come back)
//
// Sound: the BOOT side button toggles mute. The device boots muted the first
// time and remembers the last setting.
#include <Arduino.h>
#include <LittleFS.h>

#include "brain/brain.h"
#include "listen/listen_engine.h"
#include "phrases/phrase_bank.h"
#include "synth/synth_engine.h"
#include "hal/board.h"
#include "ui/bringup_screen.h"
#include "ui/lvgl_port.h"
#include "ui/translator_screen.h"
#include "version.h"

static hal::BoardInfo g_info;
static bool g_powerOk = false;
static uint32_t g_speechEndAt = 0;

static void onTap() { brain::onTap(); }

static void toggleMute() {
  synth::setMuted(!synth::muted());
  brain::setMuted(synth::muted());
  Serial.printf("sound %s\n", synth::muted() ? "muted" : "on");
}

// BOOT button: short press toggles mute. Long press is reserved for settings.
static void pollBootButton(uint32_t now) {
  static bool was = false;
  static uint32_t downAt = 0;
  bool down = hal::bootButtonPressed();
  if (down && !was) downAt = now;
  if (!down && was && now - downAt >= 30 && now - downAt < 1000) toggleMute();
  was = down;
}

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
      else if (!strcmp(cmd, "mute")) toggleMute();
      else if (!strcmp(cmd, "vol")) { synth::setVolume((uint8_t)arg); Serial.printf("volume %d\n", arg); }
      else if (!strcmp(cmd, "audio"))
        Serial.printf("synth %s, %s, blocks %lu, peak %.2f, playing %d\n", synth::running() ? "running" : "stopped",
                      synth::muted() ? "muted" : "on", (unsigned long)synth::blocksRendered(), synth::lastPeak(),
                      synth::playing());
      else if (!strcmp(cmd, "amp")) { hal::audioOut().setAmpEnabled(arg != 0); hal::debugAudio(Serial); }
      else if (!strcmp(cmd, "codec")) hal::debugAudio(Serial);
      else if (!strcmp(cmd, "tone") && strchr(line, ' ') && arg == 0) { synth::noteOff(); Serial.println("tone off"); }
      else if (!strcmp(cmd, "tone")) { uint8_t n[4] = {(uint8_t)(arg ? arg : 72), 0, 0, 0}; synth::noteOn(n, 100); Serial.println("tone on (tone 0 to stop)"); }
      else if (!strcmp(cmd, "listen")) { listen::setEnabled(arg != 0); Serial.printf("listening %s\n", arg ? "on" : "off"); }
      else if (!strcmp(cmd, "sens")) { listen::setSensitivityDb(arg); Serial.printf("sensitivity %d dB\n", listen::sensitivityDb()); }
      else if (!strcmp(cmd, "mic"))
        Serial.printf("listen %s %s, frames %lu, level %.1f dB, floor %.1f dB, thr +%d, pitch %.0f Hz, flatness %.2f, %s\n",
                      listen::running() ? "running" : "stopped", listen::enabled() ? "enabled" : "disabled",
                      (unsigned long)listen::frames(), listen::levelDb(), listen::noiseFloorDb(), listen::sensitivityDb(),
                      listen::lastPitchHz(), listen::lastFlatness(), listen::active() ? "ACTIVE" : "quiet");
      else if (!strcmp(cmd, "micpeak")) Serial.printf("listen peak since last ask %.1f dB (floor %.1f)\n", listen::peakHoldDb(), listen::noiseFloorDb());
      else if (!strcmp(cmd, "gate")) { listen::setGate(arg != 0); Serial.printf("self-hearing gate %s\n", arg ? "on" : "OFF (bench test)"); }
      else if (!strcmp(cmd, "mict")) {
        uint32_t r, f, p, t;
        listen::timing(r, f, p, t);
        Serial.printf("listen frame: read %lu us, fft %lu us, pitch %lu us, total %lu us\n", (unsigned long)r,
                      (unsigned long)f, (unsigned long)p, (unsigned long)t);
      }
      else if (!strcmp(cmd, "creg")) {
        unsigned reg = 0, val = 0;
        if (sscanf(line, "creg %x %x", &reg, &val) == 2) Serial.printf("codec reg %02X <- %02X: %s\n", reg, val, hal::debugCodecWrite(reg, val) ? "ok" : "nack");
      }
      else if (!strcmp(cmd, "rails")) hal::debugRails(Serial);
      else if (!strcmp(cmd, "reload")) Serial.printf("phrases: %d (%s)\n", phrases::reload(), phrases::source());
      else if (!strcmp(cmd, "phrases")) {
        for (int i = 0; i < phrases::count(); i++) {
          const phrases::Phrase *p = phrases::at(i);
          Serial.printf("  %-10s trig %d chords %d next %-8s %s\n", p->id.c_str(), (int)p->trigger,
                        (int)p->chords.size(), p->next.empty() ? "-" : p->next.c_str(), p->text.c_str());
        }
      }
      else Serial.println("commands: tap, speech, whistle <midi>, pickup, shake, shift, b <0-100>, mute, vol <0-100>, audio, reload, phrases, m0");
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
  Serial.printf("display %s, touch %s (%s), power %s\n", displayOk ? "ok" : "FAILED", touchOk ? "ok" : "FAILED",
                hal::touch().chipName(), g_powerOk ? "ok" : "FAILED");
  synth::begin(hal::audioOut(), 22050);
  synth::setMuted(synth::muted());  // applies the remembered state to the amplifier pin
  listen::begin(hal::audioIn(), 22050);

  if (displayOk && ui::lvglBegin(hal::display(), hal::touch())) {
    ui::translator::create();
    ui::translator::setTapHandler(onTap);
  }
  if (!LittleFS.begin(false)) Serial.println("littlefs: mount failed (run: pio run -t uploadfs)");
  phrases::begin();
  brain::begin();
  brain::setMuted(synth::muted());
  Serial.printf("ready, %d phrases (%s), sound %s\n", phrases::count(), phrases::source(),
                synth::muted() ? "muted (press BOOT to unmute)" : "on");
}

void loop() {
  uint32_t now = millis();
  handleSerial();
  pollBootButton(now);
  if (g_speechEndAt && now >= g_speechEndAt) {
    g_speechEndAt = 0;
    brain::onSpeechEnd();
  }
  listen::Event ev;
  while (listen::pollEvent(ev)) {
    switch (ev.type) {
      case listen::EventType::SpeechStart: Serial.println("listen: speech start"); brain::onSpeechStart(); break;
      case listen::EventType::SpeechEnd: Serial.println("listen: speech end"); brain::onSpeechEnd(); break;
      case listen::EventType::Whistle:
        Serial.printf("listen: whistle midi %d %+d cents\n", ev.midi, ev.cents);
        brain::onWhistle(ev.midi, ev.cents);
        break;
    }
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
