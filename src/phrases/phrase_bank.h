// Phrases: chords plus the English line that types out afterwards. M2 ships a
// built-in placeholder set; M3 loads the same shape from data/phrases.json.
#pragma once
#include <stdint.h>

namespace phrases {

enum class Trigger : uint8_t { Tap, HeardSpeech, HeardWhistle, PickedUp, Shaken, Idle };

struct Chord {
  uint8_t notes[4];  // MIDI, 0 = unused
  uint16_t ms;
};

struct Phrase {
  const char *id;
  Trigger trigger;
  const char *next;  // id of the phrase a tap continues to, or nullptr
  Chord chords[6];
  uint8_t chordCount;
  const char *text;
};

const Phrase *pick(Trigger t, const char *avoidId);  // random matching phrase, not avoidId
const Phrase *byId(const char *id);
int count();

}  // namespace phrases
