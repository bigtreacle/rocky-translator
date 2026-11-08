// Phrases: chords plus the English line that types out afterwards.
//
// Loaded from /phrases.json on LittleFS at boot (DESIGN.md §10). If the file
// is missing or unusable, a small built-in set keeps the device talking.
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

namespace phrases {

enum class Trigger : uint8_t { Tap, HeardSpeech, HeardWhistle, PickedUp, Shaken, Idle };

struct Chord {
  uint8_t notes[4] = {0, 0, 0, 0};  // MIDI, 0 = unused; all zero = rest
  uint16_t ms = 300;
  uint16_t gapMs = 0;               // silence after the chord
  uint8_t velocity = 100;
};

struct Phrase {
  std::string id;
  std::string mood;
  Trigger trigger = Trigger::Tap;
  std::string next;  // id a tap continues to, or empty
  std::vector<Chord> chords;
  std::string text;
};

// Mounts nothing itself: call after LittleFS.begin(). Returns the number of
// phrases loaded; `source` says "json" or "builtin".
int begin();
int reload();
const char *source();

const Phrase *pick(Trigger t, const char *avoidId);  // random matching phrase, not avoidId
const Phrase *byId(const char *id);
int count();
const Phrase *at(int i);

}  // namespace phrases
