#include "power_manager.h"

#include <Arduino.h>
#include <Preferences.h>

#include "../brain/brain.h"
#include "../hal/board.h"
#include "../listen/listen_engine.h"
#include "../synth/synth_engine.h"
#include "../ui/standby_screen.h"
#include "../ui/translator_screen.h"

namespace power {
namespace {

Mode g_mode = Mode::Active;
uint32_t g_lastActivity = 0;
uint32_t g_lastPoll = 0;
hal::PowerState g_ps;
uint8_t g_cap = 100;
bool g_fast = false;
bool g_faceDownHold = false;  // off because face-down: only face-up, touch or button wake it

constexpr uint8_t STANDBY_BRIGHTNESS = 15;
constexpr uint8_t BATTERY_CAP = 60;

uint32_t standbyAfterMs() { return g_fast ? 5000 : (g_ps.usbPresent ? 60000 : 30000); }
uint32_t offAfterMs() { return g_fast ? 15000 : (g_ps.usbPresent ? 300000 : 120000); }

uint8_t activeBrightness() {
  uint8_t cap = g_cap;
  if (!g_ps.usbPresent && cap > BATTERY_CAP) cap = BATTERY_CAP;
  return cap;
}

void applyActive() {
  hal::display().setPower(true);
  hal::display().setBrightness(activeBrightness());
  ui::standby::show(false);
  ui::translator::setVisible(true);
  listen::pause(false);
}

}  // namespace

void begin() {
  Preferences p;
  p.begin("rocky", true);
  g_cap = p.getUChar("bright", 100);
  p.end();
  g_lastActivity = millis();
  g_ps = hal::power().read();
  applyActive();
  g_mode = Mode::Active;
}

void activity(uint32_t now) {
  g_lastActivity = now;
  if (g_mode != Mode::Active && !g_faceDownHold) wake(now);
}

void wake(uint32_t now) {
  g_lastActivity = now;
  if (g_mode != Mode::Active) Serial.println("power: wake");
  g_faceDownHold = false;
  g_mode = Mode::Active;
  applyActive();
}

void standby(uint32_t now) {
  if (g_mode == Mode::Standby) return;
  Serial.println("power: standby");
  g_mode = Mode::Standby;
  g_lastActivity = now;
  ui::translator::setVisible(false);
  ui::standby::show(true);
  hal::display().setBrightness(STANDBY_BRIGHTNESS);
  // Listening stays on: sound wakes us from standby.
}

void off(uint32_t now) {
  if (g_mode == Mode::Off) return;
  Serial.println("power: off");
  g_mode = Mode::Off;
  g_lastActivity = now;
  ui::standby::show(false);
  ui::translator::setVisible(false);
  hal::display().setBrightness(0);
  hal::display().setPower(false);
  listen::pause(true);
  synth::noteOff();
}

void offFaceDown(uint32_t now) {
  off(now);
  g_faceDownHold = true;
}

void faceUp(uint32_t now) {
  if (g_faceDownHold) wake(now);
}

void onButton(hal::ButtonEvent e, uint32_t now) {
  if (e == hal::ButtonEvent::Long) {
    Serial.println("power: long press, shutting down");
    hal::display().setBrightness(0);
    hal::display().setPower(false);
    delay(50);
    hal::power().shutdown();
    return;
  }
  if (e == hal::ButtonEvent::Short) {
    if (g_mode == Mode::Active) off(now);
    else wake(now);
  }
}

void tick(uint32_t now) {
  if (now - g_lastPoll >= 5000 || g_lastPoll == 0) {
    g_lastPoll = now;
    bool wasUsb = g_ps.usbPresent;
    g_ps = hal::power().read();
    brain::setBattery(g_ps.batteryPercent, g_ps.charging || g_ps.usbPresent);
    if (g_mode == Mode::Active && wasUsb != g_ps.usbPresent) hal::display().setBrightness(activeBrightness());
  }
  // Sound counts as activity in standby only (the mic is paused when off).
  if (g_mode == Mode::Standby && listen::active()) activity(now);

  uint32_t idle = now - g_lastActivity;
  if (g_mode == Mode::Active && idle >= standbyAfterMs()) standby(now);
  else if (g_mode == Mode::Standby && idle >= offAfterMs()) off(now);

  if (g_mode == Mode::Off && !g_ps.usbPresent) {
    // On battery: sleep between polls. USB stays awake so the serial console and
    // flashing keep working (the USB link would drop in light sleep).
    hal::WakeCause c = hal::lightSleep(250);
    if (c == hal::WakeCause::Touch || c == hal::WakeCause::Button) wake(millis());
  }
}

Mode mode() { return g_mode; }

void setBrightnessCap(uint8_t percent) {
  if (percent < 20) percent = 20;
  if (percent > 100) percent = 100;
  g_cap = percent;
  Preferences p;
  p.begin("rocky", false);
  p.putUChar("bright", g_cap);
  p.end();
  if (g_mode == Mode::Active) hal::display().setBrightness(activeBrightness());
}
uint8_t brightnessCap() { return g_cap; }
void setTestTimers(bool fast) { g_fast = fast; }
const hal::PowerState &lastPowerState() { return g_ps; }

}  // namespace power
