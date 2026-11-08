#include "phrase_bank.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace phrases {
namespace {

constexpr const char *PATH = "/phrases.json";
constexpr size_t MAX_CHORDS = 8;

std::vector<Phrase> g_bank;
const char *g_source = "none";

bool parseTrigger(const char *s, Trigger &out) {
  if (!s) return false;
  if (!strcmp(s, "tap")) out = Trigger::Tap;
  else if (!strcmp(s, "heard_speech")) out = Trigger::HeardSpeech;
  else if (!strcmp(s, "heard_whistle")) out = Trigger::HeardWhistle;
  else if (!strcmp(s, "picked_up")) out = Trigger::PickedUp;
  else if (!strcmp(s, "shaken")) out = Trigger::Shaken;
  else if (!strcmp(s, "idle")) out = Trigger::Idle;
  else return false;
  return true;
}

// A phrase or two of original text so the device runs with no filesystem.
void loadBuiltin() {
  g_bank.clear();
  Phrase a;
  a.id = "builtin_tap";
  a.trigger = Trigger::Tap;
  a.chords = {{{60, 64, 67, 0}, 320, 0, 100}, {{62, 65, 69, 0}, 420, 0, 100}};
  a.text = "No phrase file found. Upload data/phrases.json.";
  g_bank.push_back(a);
  Phrase b = a;
  b.id = "builtin_speech";
  b.trigger = Trigger::HeardSpeech;
  b.text = "I hear you. I have nothing to say yet.";
  g_bank.push_back(b);
  Phrase c = a;
  c.id = "builtin_whistle";
  c.trigger = Trigger::HeardWhistle;
  g_bank.push_back(c);
  Phrase d = a;
  d.id = "builtin_pickup";
  d.trigger = Trigger::PickedUp;
  g_bank.push_back(d);
  Phrase e = a;
  e.id = "builtin_shake";
  e.trigger = Trigger::Shaken;
  g_bank.push_back(e);
  g_source = "builtin";
}

bool loadJson() {
  File f = LittleFS.open(PATH, "r");
  if (!f) {
    Serial.printf("phrases: %s not found\n", PATH);
    return false;
  }
  JsonDocument doc;  // ArduinoJson 7 grows as needed (PSRAM via the default allocator)
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    Serial.printf("phrases: JSON error: %s\n", err.c_str());
    return false;
  }
  JsonArray arr = doc["phrases"].as<JsonArray>();
  if (arr.isNull()) {
    Serial.println("phrases: no 'phrases' array");
    return false;
  }
  std::vector<Phrase> bank;
  int skipped = 0;
  for (JsonObject o : arr) {
    Phrase p;
    const char *id = o["id"] | (const char *)nullptr;
    const char *text = o["text"] | (const char *)nullptr;
    if (!id || !text || !parseTrigger(o["trigger"] | (const char *)nullptr, p.trigger)) {
      skipped++;
      Serial.printf("phrases: skipping entry %d (missing id/text or bad trigger)\n", (int)bank.size() + skipped);
      continue;
    }
    p.id = id;
    p.text = text;
    p.mood = o["mood"] | "";
    p.next = o["next"] | "";
    JsonArray chords = o["chords"].as<JsonArray>();
    for (JsonObject c : chords) {
      if (p.chords.size() >= MAX_CHORDS) break;
      Chord ch;
      int n = 0;
      JsonArray notes = c["notes"].as<JsonArray>();
      for (JsonVariant v : notes) {
        int midi = v.as<int>();
        if (n < 4 && midi > 0 && midi < 128) ch.notes[n++] = (uint8_t)midi;
      }
      ch.ms = (uint16_t)constrain(c["ms"] | 300, 30, 5000);
      ch.gapMs = (uint16_t)constrain(c["gap_ms"] | 0, 0, 5000);
      ch.velocity = (uint8_t)constrain(c["velocity"] | 100, 1, 127);
      p.chords.push_back(ch);
    }
    if (p.chords.empty()) {
      skipped++;
      Serial.printf("phrases: skipping %s (no chords)\n", id);
      continue;
    }
    bank.push_back(std::move(p));
  }
  if (bank.empty()) {
    Serial.println("phrases: file had no usable phrases");
    return false;
  }
  g_bank = std::move(bank);
  g_source = "json";
  Serial.printf("phrases: %d loaded from %s%s\n", (int)g_bank.size(), PATH,
                skipped ? " (some skipped, see above)" : "");
  return true;
}

}  // namespace

int begin() { return reload(); }

int reload() {
  if (!loadJson()) loadBuiltin();
  return (int)g_bank.size();
}

const char *source() { return g_source; }
int count() { return (int)g_bank.size(); }
const Phrase *at(int i) { return (i >= 0 && i < (int)g_bank.size()) ? &g_bank[i] : nullptr; }

const Phrase *byId(const char *id) {
  if (!id || !*id) return nullptr;
  for (const Phrase &p : g_bank)
    if (p.id == id) return &p;
  return nullptr;
}

const Phrase *pick(Trigger t, const char *avoidId) {
  std::vector<const Phrase *> c;
  for (const Phrase &p : g_bank)
    if (p.trigger == t && !(avoidId && p.id == avoidId)) c.push_back(&p);
  if (c.empty())
    for (const Phrase &p : g_bank)  // the avoided one beats nothing
      if (p.trigger == t) c.push_back(&p);
  if (c.empty()) return nullptr;
  return c[random((long)c.size())];
}

}  // namespace phrases
