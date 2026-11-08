// The interaction state machine (DESIGN.md §8). Owns the conversation, drives
// the UI model, and in M2 "plays" chords on a timing stub until the synth
// exists: the stub produces the spectrum peaks and note names the real synth
// will later report.
#pragma once
#include <stdint.h>

#include "../ui/ui_model.h"

namespace brain {

void begin();
void tick(uint32_t nowMs);  // call every loop

// Events from the outside world (touch, mic, IMU, serial simulation).
void onTap();
void onSpeechStart();
void onSpeechEnd();
void onWhistle(int midi, int cents);
void onPickedUp();
void onShaken();

void setBattery(int percent, bool charging);

const ui::UiModel &model();
const char *stateName();

}  // namespace brain
