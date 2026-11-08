// Chord synth (DESIGN.md §6): 4 wavetable voices with ADSR, detune and vibrato,
// soft-limited and written to hal::AudioOut from its own task on core 0.
// The brain is the conductor: noteOn() at each chord start, noteOff() at its
// end. Muted, the task keeps running and renders silence, so unmuting is
// instant and click-free.
#pragma once
#include <stdint.h>

#include "../hal/audio_out.h"

namespace synth {

bool begin(hal::AudioOut &out, uint32_t sampleRate);  // starts the task; false if audio is unavailable
void noteOn(const uint8_t notes[4], uint8_t velocity);  // 0 entries are ignored; all zero = noteOff
void noteOff();
bool playing();  // any voice sounding (including release tails)

void setMuted(bool muted);
bool muted();
void setVolume(uint8_t percent);  // codec volume
void setBassCut(bool on);

// Diagnostics for the serial console.
uint32_t blocksRendered();
float lastPeak();  // 0..1, pre-limiter, of the last rendered block
bool running();

}  // namespace synth
