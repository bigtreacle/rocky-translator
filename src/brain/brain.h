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
void setMuted(bool muted);

// Settings (saved in NVS by the brain)
void setIdleChatter(bool on);
void setChatterTestCadence(bool fast);  // bench only: every 60-90 s instead of 3-8 min
bool idleChatter();
void setMirrorReply(bool on);
bool mirrorReply();
// The power manager says when chatter is allowed (screen active, not on a low battery).
void setChatterAllowed(bool allowed);
bool speaking();  // a phrase is sounding or typing

const ui::UiModel &model();
const char *stateName();

}  // namespace brain
