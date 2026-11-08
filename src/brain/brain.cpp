#include "brain.h"

#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../phrases/phrase_bank.h"
#include "../synth/synth_engine.h"
#include "../ui/translator_screen.h"

namespace brain {
namespace {

enum class State { Listening, Receiving, NoteShown, Speaking, Translating };

State g_state = State::Listening;
ui::UiModel g_model;
uint32_t g_now = 0;
uint32_t g_stateSince = 0;

const phrases::Phrase *g_phrase = nullptr;
const phrases::Phrase *g_lastSpoken = nullptr;
int g_chord = -1;
uint32_t g_chordStart = 0;
bool g_inGap = false;
uint32_t g_lastTapMs = 0;
int g_mirrorRoot = 0;  // whistle mirror reply, 0 = none pending
uint32_t g_lastShift = 0;
float g_floor[24];

const char *NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void noteName(int midi, char *out, size_t n) {
  snprintf(out, n, "%s%d", NOTE_NAMES[midi % 12], midi / 12 - 1);
}

// Band index for a frequency: 24 log-spaced bands from 80 Hz to 8 kHz, as the
// listening engine will produce them.
float bandOf(float hz) { return 24.0f * log10f(hz / 80.0f) / 2.0f; }
float hzOf(int midi) { return 440.0f * powf(2.0f, (midi - 69) / 12.0f); }

void setState(State s) {
  g_state = s;
  g_stateSince = g_now;
}

void decayBands(float toward, float rate) {
  for (int i = 0; i < 24; i++) g_model.bands[i] += (toward - g_model.bands[i]) * rate;
}

// Low, slowly wandering noise floor: what an empty room looks like.
void idleBands() {
  for (int i = 0; i < 24; i++) {
    g_floor[i] += ((float)random(-100, 101) / 100.0f) * 0.01f;
    float lo = 0.03f, hi = 0.10f - i * 0.002f;
    if (g_floor[i] < lo) g_floor[i] = lo;
    if (g_floor[i] > hi) g_floor[i] = hi;
    g_model.bands[i] += (g_floor[i] - g_model.bands[i]) * 0.3f;
  }
}

// Speech-like: energy in the low-mid bands, moving fast.
void speechBands() {
  for (int i = 0; i < 24; i++) {
    float centre = 7.0f + 4.0f * sinf(g_now / 180.0f) ;
    float d = (i - centre) / 5.0f;
    float target = 0.15f + 0.7f * expf(-d * d) * (0.5f + 0.5f * ((float)random(0, 100) / 100.0f));
    g_model.bands[i] += (target - g_model.bands[i]) * 0.5f;
  }
}

// Synth-driven: peaks at the chord's notes with a simple envelope, a little
// noise so it looks alive.
void chordBands(const phrases::Chord &c, uint32_t elapsed) {
  float env;
  if (elapsed < 30) env = elapsed / 30.0f;                       // attack
  else if (elapsed < 110) env = 1.0f - 0.3f * (elapsed - 30) / 80.0f;  // decay to 0.7
  else env = 0.7f;
  if (c.ms > 120 && elapsed > c.ms - 120) env *= (c.ms - elapsed) / 120.0f;  // release
  if (env < 0) env = 0;
  float target[24] = {};
  for (int n = 0; n < 4; n++) {
    if (!c.notes[n]) continue;
    for (int harm = 1; harm <= 3; harm++) {
      float b = bandOf(hzOf(c.notes[n]) * harm);
      float amp = env * (harm == 1 ? 0.95f : harm == 2 ? 0.45f : 0.25f);
      for (int i = 0; i < 24; i++) {
        float d = i - b;
        float v = amp * expf(-d * d / 0.9f);
        if (v > target[i]) target[i] = v;
      }
    }
  }
  for (int i = 0; i < 24; i++) {
    float t = target[i] + 0.03f + 0.03f * ((float)random(0, 100) / 100.0f);
    if (t > 1) t = 1;
    g_model.bands[i] += (t - g_model.bands[i]) * 0.6f;
  }
}

void showChordNotes(const phrases::Chord &c) {
  (void)c;
  g_model.note[0] = 0;
  for (int n = 0; n < 4; n++) {
    if (!c.notes[n]) continue;
    char nm[8];
    noteName(c.notes[n], nm, sizeof nm);
    if (g_model.note[0]) strncat(g_model.note, "  ", sizeof g_model.note - strlen(g_model.note) - 1);
    strncat(g_model.note, nm, sizeof g_model.note - strlen(g_model.note) - 1);
  }
  g_model.noteIsHeard = false;
}

void soundChord(const phrases::Chord &c) { synth::noteOn(c.notes, c.velocity); }

void startPhrase(const phrases::Phrase *p) {
  if (!p) return;
  g_phrase = p;
  g_lastSpoken = p;
  g_chord = 0;
  g_chordStart = g_now;
  g_inGap = false;
  g_mirrorRoot = 0;
  showChordNotes(p->chords[0]);
  soundChord(p->chords[0]);
  g_model.status = ui::Status::Speaking;
  setState(State::Speaking);
  Serial.printf("brain: speaking %s\n", p->id.c_str());
}

phrases::Phrase g_mirror;  // built on the fly for a whistle reply

void startMirror(int midi) {
  // Major triad on the whistled note, twice, then the root an octave up.
  uint8_t r = (uint8_t)midi;
  while (r < 60) r += 12;
  while (r > 84) r -= 12;
  g_mirror = phrases::Phrase();
  g_mirror.id = "mirror";
  g_mirror.trigger = phrases::Trigger::HeardWhistle;
  g_mirror.chords = {{{r, (uint8_t)(r + 4), (uint8_t)(r + 7), 0}, 300, 0, 100},
                     {{r, (uint8_t)(r + 4), (uint8_t)(r + 7), 0}, 300, 0, 100},
                     {{(uint8_t)(r + 12), 0, 0, 0}, 450, 0, 100}};
  char nm[8];
  noteName(midi, nm, sizeof nm);
  g_mirror.text = std::string("You said: ") + nm + ". I say it back.";
  startPhrase(&g_mirror);
}

void advanceSpeaking() {
  const phrases::Chord &c = g_phrase->chords[g_chord];
  uint32_t elapsed = g_now - g_chordStart;
  if (g_inGap) {
    decayBands(0.04f, 0.3f);
    if (elapsed < c.gapMs) return;
    g_inGap = false;
    g_chord++;
    g_chordStart = g_now;
    if (g_chord < (int)g_phrase->chords.size()) {
      showChordNotes(g_phrase->chords[g_chord]);
      soundChord(g_phrase->chords[g_chord]);
    }
  }
  if (g_chord >= (int)g_phrase->chords.size()) {
    synth::noteOff();
    g_model.status = ui::Status::Translating;
    setState(State::Translating);
    ui::translator::startTyping(g_phrase->text.c_str());
    return;
  }
  const phrases::Chord &cur = g_phrase->chords[g_chord];
  elapsed = g_now - g_chordStart;
  chordBands(cur, elapsed);
  if (elapsed >= cur.ms) {
    if (cur.gapMs > 0) {
      g_inGap = true;
      g_chordStart = g_now;
      g_model.note[0] = 0;
      synth::noteOff();
      return;
    }
    g_chord++;
    g_chordStart = g_now;
    if (g_chord < (int)g_phrase->chords.size()) {
      showChordNotes(g_phrase->chords[g_chord]);
      soundChord(g_phrase->chords[g_chord]);
    }
  }
}

}  // namespace

void begin() {
  for (int i = 0; i < 24; i++) {
    g_floor[i] = 0.06f;
    g_model.bands[i] = 0.05f;
  }
  g_model.status = ui::Status::Listening;
  g_model.note[0] = 0;
  g_now = millis();
  setState(State::Listening);
}

void tick(uint32_t nowMs) {
  g_now = nowMs;
  switch (g_state) {
    case State::Listening:
      idleBands();
      break;
    case State::Receiving:
      speechBands();
      break;
    case State::NoteShown:
      decayBands(0.05f, 0.1f);
      if (g_now - g_stateSince > 700) {
        if (g_mirrorRoot) startMirror(g_mirrorRoot);
        else startPhrase(phrases::pick(phrases::Trigger::HeardWhistle, nullptr));
      }
      break;
    case State::Speaking:
      advanceSpeaking();
      break;
    case State::Translating:
      decayBands(0.05f, 0.08f);
      if (ui::translator::typingDone()) {
        g_model.status = ui::Status::Listening;
        setState(State::Listening);
      }
      break;
  }
  if (g_now - g_lastShift > 180000UL) {
    g_lastShift = g_now;
    ui::translator::shiftForBurnIn();
  }
}

void onTap() {
  Serial.printf("brain: tap (%s)\n", stateName());
  if (g_state == State::Speaking) return;  // let it finish
  const phrases::Phrase *next = nullptr;
  bool inExchange = g_lastSpoken && (g_now - g_lastTapMs) < 30000UL;
  g_lastTapMs = g_now;
  if (inExchange && !g_lastSpoken->next.empty()) next = phrases::byId(g_lastSpoken->next.c_str());
  if (!next) next = phrases::pick(phrases::Trigger::Tap, g_lastSpoken ? g_lastSpoken->id.c_str() : nullptr);
  startPhrase(next);
}

void onSpeechStart() {
  if (g_state == State::Speaking || g_state == State::Translating) return;
  g_model.status = ui::Status::Receiving;
  g_model.note[0] = 0;
  setState(State::Receiving);
}

void onSpeechEnd() {
  if (g_state != State::Receiving) return;
  startPhrase(phrases::pick(phrases::Trigger::HeardSpeech, g_lastSpoken ? g_lastSpoken->id.c_str() : nullptr));
}

void onWhistle(int midi, int cents) {
  if (g_state == State::Speaking || g_state == State::Translating) return;
  char nm[8];
  noteName(midi, nm, sizeof nm);
  snprintf(g_model.note, sizeof g_model.note, "%s %+d", nm, cents);
  g_model.noteIsHeard = true;
  g_model.status = ui::Status::Receiving;
  g_mirrorRoot = midi;
  setState(State::NoteShown);
}

void onPickedUp() {
  if (g_state == State::Speaking || g_state == State::Translating) return;
  startPhrase(phrases::pick(phrases::Trigger::PickedUp, nullptr));
}

void onShaken() {
  if (g_state == State::Speaking) return;
  startPhrase(phrases::pick(phrases::Trigger::Shaken, nullptr));
}

void setMuted(bool muted) { g_model.muted = muted; }

void setBattery(int percent, bool charging) {
  g_model.batteryPercent = percent;
  g_model.charging = charging;
}

const ui::UiModel &model() { return g_model; }

const char *stateName() {
  switch (g_state) {
    case State::Listening: return "listening";
    case State::Receiving: return "receiving";
    case State::NoteShown: return "note";
    case State::Speaking: return "speaking";
    case State::Translating: return "translating";
  }
  return "?";
}

}  // namespace brain
