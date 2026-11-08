#include "listen_engine.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_dsp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

#include "../synth/synth_engine.h"
#include "pitch_yin.h"

namespace listen {
namespace {

constexpr int N = 1024;          // FFT size
constexpr int HOP = 735;         // 30 frames/s at 22.05 kHz
constexpr int BANDS = 24;
constexpr float BAND_LO = 80.0f, BAND_HI = 8000.0f;
constexpr float VAD_LO = 150.0f, VAD_HI = 4000.0f;
constexpr int SPEECH_WINDOW = 9;          // 300 ms window...
constexpr int SPEECH_START_HITS = 6;      // ...with at least this many loud frames (speech has gaps)
constexpr int SPEECH_END_FRAMES = 21;    // 700 ms below
constexpr int WHISTLE_FRAMES = 12;       // 400 ms of stable pitch
constexpr uint32_t SELF_COOLDOWN_MS = 300;

hal::AudioIn *g_in = nullptr;
uint32_t g_rate = 22050;
TaskHandle_t g_task = nullptr;
QueueHandle_t g_events = nullptr;
volatile bool g_enabled = true;
volatile int g_sensDb = 10;
volatile bool g_running = false;

// Shared with the loop task
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
float g_bandsOut[BANDS];
volatile bool g_bandsFresh = false;
volatile float g_levelDb = -100, g_floorDb = -60, g_pitchHz = 0, g_flatness = 1;
volatile float g_peakHoldDb = -200;  // loudest level since the last peakHoldDb() call
volatile bool g_gate = true;          // self-hearing gate; off only for bench tests
volatile bool g_active = false;
volatile uint32_t g_frames = 0;
volatile uint32_t g_usRead = 0, g_usFft = 0, g_usPitch = 0, g_usTotal = 0;  // last frame, microseconds

// Working buffers (internal RAM; the FFT wants 16-byte alignment)
__attribute__((aligned(16))) float g_fft[2 * N];
float g_ring[N];
float g_window[N];
int16_t g_pcm[HOP];
float g_mag[N / 2];
int g_bandBin[BANDS + 1];
float g_bandSmooth[BANDS];

void buildBands() {
  float binHz = (float)g_rate / N;
  for (int b = 0; b <= BANDS; b++) {
    float f = BAND_LO * powf(BAND_HI / BAND_LO, (float)b / BANDS);
    int bin = (int)(f / binHz + 0.5f);
    if (bin < 1) bin = 1;
    if (bin > N / 2 - 1) bin = N / 2 - 1;
    g_bandBin[b] = bin;
  }
  for (int b = 1; b <= BANDS; b++)  // every band at least one bin wide
    if (g_bandBin[b] <= g_bandBin[b - 1]) g_bandBin[b] = g_bandBin[b - 1] + 1;
}

float bandDb(int b) {
  int lo = g_bandBin[b], hi = g_bandBin[b + 1];
  float e = 0;
  for (int i = lo; i < hi; i++) e += g_mag[i];
  e /= (hi - lo);
  return 10.0f * log10f(e + 1e-12f);
}

// Level for voice activity: the loudest band between fLo and fHi. Speech and
// whistles are peaky, room noise is flat, so a max separates them better than
// an average does.
float peakBandDb(float fLo, float fHi) {
  float binHz = (float)g_rate / N;
  float best = -200;
  for (int b = 0; b < BANDS; b++) {
    float fc = (g_bandBin[b] + g_bandBin[b + 1]) * 0.5f * binHz;
    if (fc < fLo || fc > fHi) continue;
    float v = bandDb(b);
    if (v > best) best = v;
  }
  return best;
}

// Spectral flatness over 300 Hz..5 kHz: ~1 for noise, small for a tone.
float flatness() {
  float binHz = (float)g_rate / N;
  int lo = (int)(300 / binHz), hi = (int)(5000 / binHz);
  float logSum = 0, sum = 0;
  for (int i = lo; i < hi; i++) {
    logSum += logf(g_mag[i] + 1e-12f);
    sum += g_mag[i];
  }
  int n = hi - lo;
  return expf(logSum / n) / (sum / n + 1e-12f);
}

void publishBands(float floorDb) {
  float out[BANDS];
  for (int b = 0; b < BANDS; b++) {
    float v = (bandDb(b) - floorDb + 6.0f) / 45.0f;  // 6 dB under the floor is black, +39 dB is full
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    float k = v > g_bandSmooth[b] ? 0.6f : 0.2f;  // fast attack, slower release
    g_bandSmooth[b] += (v - g_bandSmooth[b]) * k;
    out[b] = g_bandSmooth[b];
  }
  portENTER_CRITICAL(&g_mux);
  memcpy(g_bandsOut, out, sizeof out);
  g_bandsFresh = true;
  portEXIT_CRITICAL(&g_mux);
}

void post(EventType t, int midi = 0, int cents = 0) {
  Event e{t, midi, cents};
  xQueueSend(g_events, &e, 0);
}

void listenTask(void *) {
  float floorDb = -60;
  bool floorSeeded = false;
  uint32_t loudBits = 0;  // one bit per recent frame, newest in bit 0
  int below = 0;
  int loudRun = 0;
  bool inSegment = false;
  bool whistleSent = false;
  int stablePitch = 0;
  float lastMidiF = 0;
  float pitchAcc = 0;
  int pitchCount = 0;
  uint32_t lastSynthMs = 0;

  uint32_t framesIn = 0;
  for (;;) {
    vTaskDelay(1);  // one tick a frame keeps the idle task (and its watchdog) fed even when we lag
    uint32_t t0 = micros();
    size_t got = g_in->read(g_pcm, HOP);  // blocks ~33 ms; paces the task and drains the DMA
    uint32_t t1 = micros();
    if (got == 0) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    framesIn++;
    // slide the ring and append
    memmove(g_ring, g_ring + got, (N - got) * sizeof(float));
    for (size_t i = 0; i < got; i++) g_ring[N - got + i] = g_pcm[i] / 32768.0f;

    // Never hear ourselves: while the synth plays and shortly after, drop the frame.
    uint32_t now = millis();
    if (synth::playing()) lastSynthMs = now;
    bool selfNoise = g_gate && (synth::playing() || (now - lastSynthMs) < SELF_COOLDOWN_MS);
    if (!g_enabled || selfNoise) {
      loudBits = 0;
      below = 0;
      loudRun = 0;
      if (inSegment) {  // a segment cannot survive our own playback
        inSegment = false;
        g_active = false;
      }
      g_frames = g_frames + 1;
      continue;
    }
    if (framesIn < 3) {  // the ring is still partly zeros; do not learn a floor from silence
      g_frames = g_frames + 1;
      continue;
    }

    // Window + FFT + magnitude
    for (int i = 0; i < N; i++) {
      g_fft[2 * i] = g_ring[i] * g_window[i];
      g_fft[2 * i + 1] = 0;
    }
    dsps_fft2r_fc32(g_fft, N);
    dsps_bit_rev_fc32(g_fft, N);
    for (int i = 0; i < N / 2; i++) g_mag[i] = g_fft[2 * i] * g_fft[2 * i] + g_fft[2 * i + 1] * g_fft[2 * i + 1];
    uint32_t t2 = micros();

    float level = peakBandDb(VAD_LO, VAD_HI);
    if (!floorSeeded) {
      floorDb = level;
      floorSeeded = true;
    }
    float threshold = floorDb + g_sensDb;
    bool loud = level > threshold;
    // Noise floor: quick to fall, slow to rise, and it does not rise at all while
    // sound sits just above it (quiet speech must not become the new floor).
    if (level < floorDb) floorDb += (level - floorDb) * 0.05f;
    else if (level < floorDb + 5.0f) floorDb += (level - floorDb) * 0.01f;
    // A "loud" state that never ends is a floor seeded too low: re-seed after 20 s.
    if (loud) loudRun++;
    else loudRun = 0;
    if (loudRun > 30 * 20) floorDb = level;

    // Voice activity: start when most of the last 300 ms was loud, end after 700 ms of quiet.
    loudBits = ((loudBits << 1) | (loud ? 1u : 0u)) & ((1u << SPEECH_WINDOW) - 1);
    int hits = __builtin_popcount(loudBits);
    if (loud) below = 0;
    else below++;
    if (!inSegment && hits >= SPEECH_START_HITS) {
      inSegment = true;
      whistleSent = false;
      stablePitch = 0;
      pitchAcc = 0;
      pitchCount = 0;
      g_active = true;
      post(EventType::SpeechStart);
    }

    // Pitch and tonality, only while something is sounding
    float flat = 1;
    float hz = 0;
    uint32_t t3 = micros();
    if (loud) {
      flat = flatness();
      PitchResult pr = yinPitch(g_ring, N, (float)g_rate, 150.0f, 3000.0f, 0.15f);
      if (pr.confidence > 0.8f) hz = pr.hz;
      if (inSegment && !whistleSent && hz > 0 && flat < 0.3f) {
        float midiF = 69.0f + 12.0f * log2f(hz / 440.0f);
        if (stablePitch > 0 && fabsf(midiF - lastMidiF) < 0.5f) {
          stablePitch++;
          pitchAcc += midiF;
          pitchCount++;
        } else {
          stablePitch = 1;
          pitchAcc = midiF;
          pitchCount = 1;
        }
        lastMidiF = midiF;
        if (stablePitch >= WHISTLE_FRAMES) {
          float m = pitchAcc / pitchCount;
          int midi = (int)lroundf(m);
          int cents = (int)lroundf((m - midi) * 100.0f);
          whistleSent = true;
          post(EventType::Whistle, midi, cents);
        }
      } else if (inSegment && !whistleSent) {
        stablePitch = 0;
      }
    }
    if (inSegment && below >= SPEECH_END_FRAMES) {
      inSegment = false;
      g_active = false;
      post(EventType::SpeechEnd);
    }

    uint32_t t4 = micros();
    if (level > g_peakHoldDb) g_peakHoldDb = level;
    g_levelDb = level;
    g_floorDb = floorDb;
    g_pitchHz = hz;
    g_flatness = flat;
    publishBands(floorDb);
    g_frames = g_frames + 1;
    g_usRead = t1 - t0;
    g_usFft = t2 - t1;
    g_usPitch = t4 - t3;
    g_usTotal = micros() - t1;
  }
}

}  // namespace

bool begin(hal::AudioIn &in, uint32_t sampleRate) {
  g_in = &in;
  g_rate = sampleRate;
  Preferences p;
  p.begin("rocky", true);
  g_enabled = p.getBool("listen", true);
  g_sensDb = p.getInt("sensDb", 10);
  p.end();
  if (!in.begin(sampleRate)) {
    Serial.println("listen: microphone unavailable");
    return false;
  }
  if (dsps_fft2r_init_fc32(nullptr, N) != ESP_OK) {
    Serial.println("listen: FFT init failed");
    return false;
  }
  dsps_wind_hann_f32(g_window, N);
  buildBands();
  {  // benchmark the pitch detector once on a synthetic 440 Hz tone
    for (int i = 0; i < N; i++) g_ring[i] = 0.5f * sinf(2 * (float)M_PI * 440.0f * i / sampleRate);
    uint32_t t0 = micros();
    PitchResult pr = yinPitch(g_ring, N, (float)sampleRate, 150.0f, 3000.0f, 0.15f);
    uint32_t t1 = micros();
    float fl = 0;
    for (int i = 0; i < N; i++) { g_fft[2 * i] = g_ring[i] * g_window[i]; g_fft[2 * i + 1] = 0; }
    dsps_fft2r_fc32(g_fft, N);
    dsps_bit_rev_fc32(g_fft, N);
    for (int i = 0; i < N / 2; i++) g_mag[i] = g_fft[2 * i] * g_fft[2 * i] + g_fft[2 * i + 1] * g_fft[2 * i + 1];
    uint32_t t2 = micros();
    fl = flatness();
    uint32_t t3 = micros();
    Serial.printf("listen: benchmark yin %lu us (440 Hz tone -> %.1f Hz, conf %.2f), fft %lu us, flatness %lu us (%.3f)\n",
                  (unsigned long)(t1 - t0), pr.hz, pr.confidence, (unsigned long)(t2 - t1), (unsigned long)(t3 - t2), fl);
  }
  memset(g_ring, 0, sizeof g_ring);
  memset(g_bandSmooth, 0, sizeof g_bandSmooth);
  g_events = xQueueCreate(8, sizeof(Event));
  xTaskCreatePinnedToCore(listenTask, "listen", 16384, nullptr, 10, &g_task, 0);
  g_running = true;
  Serial.printf("listen: running, %s, sensitivity %d dB\n", g_enabled ? "enabled" : "disabled", (int)g_sensDb);
  return true;
}

void setEnabled(bool on) {
  g_enabled = on;
  Preferences p;
  p.begin("rocky", false);
  p.putBool("listen", on);
  p.end();
}
bool enabled() { return g_enabled; }

void setSensitivityDb(int db) {
  if (db < 6) db = 6;
  if (db > 24) db = 24;
  g_sensDb = db;
  Preferences p;
  p.begin("rocky", false);
  p.putInt("sensDb", db);
  p.end();
}
int sensitivityDb() { return g_sensDb; }

bool getBands(float *out24) {
  if (!g_bandsFresh) return false;
  portENTER_CRITICAL(&g_mux);
  memcpy(out24, g_bandsOut, sizeof g_bandsOut);
  g_bandsFresh = false;
  portEXIT_CRITICAL(&g_mux);
  return true;
}

bool pollEvent(Event &e) { return g_events && xQueueReceive(g_events, &e, 0) == pdTRUE; }

void timing(uint32_t &usRead, uint32_t &usFft, uint32_t &usPitch, uint32_t &usTotal) {
  usRead = g_usRead;
  usFft = g_usFft;
  usPitch = g_usPitch;
  usTotal = g_usTotal;
}
float peakHoldDb() {
  float v = g_peakHoldDb;
  g_peakHoldDb = -200;
  return v;
}
void setGate(bool on) { g_gate = on; }
float levelDb() { return g_levelDb; }
float noiseFloorDb() { return g_floorDb; }
float lastPitchHz() { return g_pitchHz; }
float lastFlatness() { return g_flatness; }
bool active() { return g_active; }
uint32_t frames() { return g_frames; }
bool running() { return g_running; }

}  // namespace listen
