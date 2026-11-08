#include "synth_engine.h"

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

namespace synth {
namespace {

constexpr int TABLE_LEN = 1024;
constexpr int BLOCK = 256;  // frames per render, 11.6 ms at 22.05 kHz
constexpr int VOICES = 4;

float g_table[TABLE_LEN];
uint32_t g_rate = 22050;
hal::AudioOut *g_out = nullptr;
QueueHandle_t g_cmds = nullptr;
TaskHandle_t g_task = nullptr;
volatile bool g_muted = true;
volatile bool g_bassCut = true;
volatile bool g_playing = false;
volatile uint32_t g_blocks = 0;
volatile float g_peak = 0;
uint8_t g_volume = 85;

struct Cmd {
  bool on;
  uint8_t notes[4];
  uint8_t velocity;
};

enum class Stage : uint8_t { Off, Attack, Decay, Sustain, Release };

struct Voice {
  Stage stage = Stage::Off;
  float phase = 0;      // 0..TABLE_LEN
  float inc = 0;        // table steps per sample at the nominal pitch
  float detune = 1.0f;  // ratio
  float env = 0;
  float target = 0;
  float rate = 0;       // env change per sample for the current stage
  float level = 0;      // velocity 0..1
  uint32_t age = 0;     // samples since noteOn, for the vibrato fade-in
};

Voice g_voices[VOICES];
const float DETUNE_CENTS[VOICES] = {0.0f, -4.0f, 3.0f, 6.0f};
float g_lfoPhase = 0;
float g_hpfState = 0, g_hpfPrev = 0;

// Envelope times
constexpr float ATTACK_MS = 12, DECAY_MS = 80, SUSTAIN = 0.8f, RELEASE_MS = 120;

float centsRatio(float c) { return powf(2.0f, c / 1200.0f); }
float midiHz(int m) { return 440.0f * powf(2.0f, (m - 69) / 12.0f); }

void setStage(Voice &v, Stage s) {
  v.stage = s;
  switch (s) {
    case Stage::Attack: v.target = 1.0f; v.rate = (1.0f - v.env) / (ATTACK_MS * 0.001f * g_rate); break;
    case Stage::Decay: v.target = SUSTAIN; v.rate = (SUSTAIN - 1.0f) / (DECAY_MS * 0.001f * g_rate); break;
    case Stage::Sustain: v.target = SUSTAIN; v.rate = 0; break;
    case Stage::Release: v.target = 0.0f; v.rate = -(v.env > 0 ? v.env : 0.0001f) / (RELEASE_MS * 0.001f * g_rate); break;
    case Stage::Off: v.target = 0; v.rate = 0; v.env = 0; break;
  }
}

void buildTable() {
  // "Eridian": mostly sine, some triangle, a touch of third harmonic. Reedy
  // but soft on a tiny speaker.
  float peak = 0;
  for (int i = 0; i < TABLE_LEN; i++) {
    float t = (float)i / TABLE_LEN;
    float sine = sinf(2 * (float)M_PI * t);
    float tri = 4.0f * fabsf(t - 0.5f) - 1.0f;
    float third = sinf(6 * (float)M_PI * t);
    g_table[i] = 0.65f * sine + 0.25f * tri + 0.10f * third;
    if (fabsf(g_table[i]) > peak) peak = fabsf(g_table[i]);
  }
  for (int i = 0; i < TABLE_LEN; i++) g_table[i] /= peak;
}

void applyCmd(const Cmd &c) {
  if (!c.on) {
    for (Voice &v : g_voices)
      if (v.stage != Stage::Off) setStage(v, Stage::Release);
    return;
  }
  // Clamp into C4..C6 by octave, then assign one voice per note. Voices that
  // were sounding restart from their current level: no click, no gap.
  int n = 0;
  for (int i = 0; i < 4 && n < VOICES; i++) {
    int midi = c.notes[i];
    if (!midi) continue;
    while (midi < 60) midi += 12;
    while (midi > 84) midi -= 12;
    Voice &v = g_voices[n];
    v.inc = midiHz(midi) * TABLE_LEN / (float)g_rate;
    v.detune = centsRatio(DETUNE_CENTS[n]);
    v.level = c.velocity / 127.0f;
    v.age = 0;
    setStage(v, Stage::Attack);
    n++;
  }
  for (; n < VOICES; n++)
    if (g_voices[n].stage != Stage::Off) setStage(g_voices[n], Stage::Release);
}

void renderBlock(int16_t *out) {
  const float lfoInc = 5.5f / g_rate;  // 5.5 Hz vibrato
  const float hpfCoef = expf(-2.0f * (float)M_PI * 220.0f / g_rate);
  float peak = 0;
  bool anyOn = false;
  for (int i = 0; i < BLOCK; i++) {
    g_lfoPhase += lfoInc;
    if (g_lfoPhase >= 1.0f) g_lfoPhase -= 1.0f;
    float lfo = sinf(2 * (float)M_PI * g_lfoPhase);
    float mix = 0;
    for (Voice &v : g_voices) {
      if (v.stage == Stage::Off) continue;
      anyOn = true;
      // envelope
      v.env += v.rate;
      if ((v.rate > 0 && v.env >= v.target) || (v.rate < 0 && v.env <= v.target)) {
        v.env = v.target;
        if (v.stage == Stage::Attack) setStage(v, Stage::Decay);
        else if (v.stage == Stage::Decay) setStage(v, Stage::Sustain);
        else if (v.stage == Stage::Release) setStage(v, Stage::Off);
      }
      // vibrato fades in over 150 ms, depth 6 cents
      float fade = v.age < (uint32_t)(0.15f * g_rate) ? v.age / (0.15f * g_rate) : 1.0f;
      float vib = 1.0f + fade * lfo * 0.0035f;  // ~6 cents peak
      v.age++;
      v.phase += v.inc * v.detune * vib;
      while (v.phase >= TABLE_LEN) v.phase -= TABLE_LEN;
      int idx = (int)v.phase;
      float frac = v.phase - idx;
      float s = g_table[idx] + (g_table[(idx + 1) & (TABLE_LEN - 1)] - g_table[idx]) * frac;
      mix += s * v.env * v.level * 0.3f;  // 4 voices at full level sum to ~1.2
    }
    if (g_bassCut) {  // one-pole high-pass around 220 Hz
      float y = hpfCoef * (g_hpfState + mix - g_hpfPrev);
      g_hpfPrev = mix;
      g_hpfState = y;
      mix = y;
    }
    float a = fabsf(mix);
    if (a > peak) peak = a;
    float lim = mix / (1.0f + a) * 1.6f;  // soft limiter, unity-ish below -6 dB
    if (lim > 0.98f) lim = 0.98f;
    if (lim < -0.98f) lim = -0.98f;
    int16_t s16 = g_muted ? 0 : (int16_t)(lim * 32767.0f);
    out[2 * i] = s16;
    out[2 * i + 1] = s16;
  }
  g_peak = peak;
  g_playing = anyOn;
}

void synthTask(void *) {
  static int16_t block[BLOCK * 2];
  Cmd c;
  for (;;) {
    while (xQueueReceive(g_cmds, &c, 0) == pdTRUE) applyCmd(c);
    renderBlock(block);
    g_out->write(block, BLOCK);  // blocks until the DMA takes it: this paces the task
    g_blocks = g_blocks + 1;
  }
}

}  // namespace

bool begin(hal::AudioOut &out, uint32_t sampleRate) {
  g_out = &out;
  g_rate = sampleRate;
  buildTable();
  Preferences p;
  p.begin("rocky", true);
  g_muted = p.getBool("muted", true);  // quiet until asked, and remembered
  g_volume = p.getUChar("volume", 85);
  g_bassCut = p.getBool("bassCut", true);
  p.end();
  if (!out.begin(sampleRate)) {
    Serial.println("synth: audio output unavailable, running silent");
    return false;
  }
  out.setVolume(g_volume);
  g_cmds = xQueueCreate(16, sizeof(Cmd));
  xTaskCreatePinnedToCore(synthTask, "synth", 8192, nullptr, 20, &g_task, 0);
  Serial.printf("synth: running at %u Hz, %s, volume %u\n", (unsigned)sampleRate, g_muted ? "muted" : "unmuted",
                g_volume);
  return true;
}

void noteOn(const uint8_t notes[4], uint8_t velocity) {
  if (!g_cmds) return;
  Cmd c{true, {notes[0], notes[1], notes[2], notes[3]}, velocity ? velocity : (uint8_t)100};
  if (!notes[0] && !notes[1] && !notes[2] && !notes[3]) c.on = false;
  xQueueSend(g_cmds, &c, 0);
}

void noteOff() {
  if (!g_cmds) return;
  Cmd c{false, {0, 0, 0, 0}, 0};
  xQueueSend(g_cmds, &c, 0);
}

bool playing() { return g_playing; }

void setMuted(bool m) {
  g_muted = m;
  if (g_out) g_out->setAmpEnabled(!m);  // no amplifier hiss while muted
  Preferences p;
  p.begin("rocky", false);
  p.putBool("muted", m);
  p.end();
}
bool muted() { return g_muted; }

void setVolume(uint8_t percent) {
  g_volume = percent > 100 ? 100 : percent;
  if (g_out) g_out->setVolume(g_volume);
  Preferences p;
  p.begin("rocky", false);
  p.putUChar("volume", g_volume);
  p.end();
}

void setBassCut(bool on) {
  g_bassCut = on;
  Preferences p;
  p.begin("rocky", false);
  p.putBool("bassCut", on);
  p.end();
}

uint32_t blocksRendered() { return g_blocks; }
float lastPeak() { return g_peak; }
bool running() { return g_task != nullptr; }

}  // namespace synth
