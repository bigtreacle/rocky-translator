// Listening engine (DESIGN.md §7): mic -> FFT -> 24 bands for the display,
// adaptive voice activity, and YIN pitch to tell a whistle or hum from speech.
// Runs in its own task on core 0; results are polled from the loop task.
#pragma once
#include <stdint.h>

#include "../hal/audio_in.h"

namespace listen {

enum class EventType : uint8_t { SpeechStart, SpeechEnd, Whistle };

struct Event {
  EventType type;
  int midi;   // Whistle only
  int cents;  // Whistle only, -50..50
};

bool begin(hal::AudioIn &in, uint32_t sampleRate);  // false if the mic is unavailable
void setEnabled(bool on);                            // listening on/off (saved)
void pause(bool paused);                             // temporary, for screen-off; not saved
bool enabled();
void setSensitivityDb(int db);                       // threshold above the noise floor, 6..24 (saved)
int sensitivityDb();

// True and fills out[24] (0..1) if a fresh frame has arrived since the last call.
bool getBands(float *out24);
bool pollEvent(Event &e);

// Diagnostics
float levelDb();
float peakHoldDb();      // loudest level since the previous call
void setGate(bool on);   // self-hearing gate off = bench test only
float noiseFloorDb();
float lastPitchHz();
float lastFlatness();
bool active();  // speech or whistle segment in progress
uint32_t frames();
bool running();
void timing(uint32_t &usRead, uint32_t &usFft, uint32_t &usPitch, uint32_t &usTotal);  // last frame

}  // namespace listen
