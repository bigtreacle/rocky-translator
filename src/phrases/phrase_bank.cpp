#include "phrase_bank.h"

#include <Arduino.h>
#include <string.h>

namespace phrases {
namespace {

// Placeholder lines of original text so the device runs; the real ones are
// Simon's to write. Voicings sit in C4..C6 for the small speaker.
const Phrase BANK[] = {
    {"greet1", Trigger::PickedUp, "greet2",
     {{{60, 64, 67, 0}, 320}, {{62, 65, 69, 0}, 420}}, 2,
     "Hello friend. You are warm today."},
    {"greet2", Trigger::Tap, nullptr,
     {{{67, 71, 74, 0}, 280}, {{65, 69, 72, 0}, 280}, {{64, 67, 72, 0}, 500}}, 3,
     "Question. Where is the loud star?"},
    {"tap1", Trigger::Tap, "tap2",
     {{{62, 66, 69, 0}, 300}, {{64, 67, 71, 0}, 300}, {{67, 71, 74, 76}, 600}}, 3,
     "I counted your heartbeats. Many. Good."},
    {"tap2", Trigger::Tap, "tap3",
     {{{60, 67, 72, 0}, 260}, {{60, 67, 72, 0}, 260}, {{59, 65, 71, 0}, 520}}, 3,
     "Your air is thin. Mine is thick. We still talk."},
    {"tap3", Trigger::Tap, nullptr,
     {{{65, 69, 72, 76}, 700}}, 1,
     "Amaze."},
    {"speech1", Trigger::HeardSpeech, nullptr,
     {{{64, 68, 71, 0}, 300}, {{66, 69, 73, 0}, 300}, {{68, 71, 75, 0}, 450}}, 3,
     "You make many sounds. I understand some. Say again, slower?"},
    {"speech2", Trigger::HeardSpeech, nullptr,
     {{{60, 63, 67, 0}, 400}, {{58, 62, 65, 0}, 500}}, 2,
     "Sad chord? No. Thinking chord."},
    {"whistle1", Trigger::HeardWhistle, nullptr,
     {{{72, 76, 79, 0}, 300}, {{72, 76, 79, 84}, 500}}, 2,
     "A pure note. You sing like my people. Almost."},
    {"shake1", Trigger::Shaken, nullptr,
     {{{70, 71, 76, 0}, 150}, {{70, 71, 76, 0}, 150}, {{69, 70, 75, 0}, 400}}, 3,
     "Stop stop stop. The room is moving!"},
    {"idle1", Trigger::Idle, nullptr,
     {{{60, 64, 67, 0}, 250}, {{0, 0, 0, 0}, 250}, {{60, 64, 67, 0}, 250}}, 3,
     "Still here. Still listening."},
};

const Phrase *g_last = nullptr;

}  // namespace

int count() { return sizeof(BANK) / sizeof(BANK[0]); }

const Phrase *byId(const char *id) {
  if (!id) return nullptr;
  for (const Phrase &p : BANK)
    if (strcmp(p.id, id) == 0) return &p;
  return nullptr;
}

const Phrase *pick(Trigger t, const char *avoidId) {
  const Phrase *candidates[16];
  int n = 0;
  for (const Phrase &p : BANK)
    if (p.trigger == t && n < 16 && !(avoidId && strcmp(p.id, avoidId) == 0)) candidates[n++] = &p;
  if (n == 0) {
    for (const Phrase &p : BANK)  // allow the avoided one rather than nothing
      if (p.trigger == t && n < 16) candidates[n++] = &p;
  }
  if (n == 0) return nullptr;
  g_last = candidates[random(n)];
  return g_last;
}

}  // namespace phrases
