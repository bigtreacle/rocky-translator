// Screen and power policy (DESIGN.md §9 burn-in, §11 power): tracks activity,
// steps Active -> Standby -> Off, drives brightness and panel power, handles
// the power button, battery-aware timers, and light sleep when off on battery.
#pragma once
#include <stdint.h>

#include "../hal/power.h"

namespace power {

enum class Mode : uint8_t { Active, Standby, Off };

void begin();
void tick(uint32_t nowMs);      // from loop
void activity(uint32_t nowMs);  // anything the user did: touch, sound, gesture, phrase

void wake(uint32_t nowMs);
void standby(uint32_t nowMs);
void off(uint32_t nowMs);       // power button or timer
void offFaceDown(uint32_t nowMs);  // face-down: only face-up, touch or the button wake it
void faceUp(uint32_t nowMs);
void onButton(hal::ButtonEvent e, uint32_t nowMs);

Mode mode();
void setBrightnessCap(uint8_t percent);  // saved
uint8_t brightnessCap();
void setTestTimers(bool fast);           // 5 s / 15 s instead of 60 s / 5 min, for the bench
const hal::PowerState &lastPowerState();

}  // namespace power
