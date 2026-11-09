#include "pitch_yin.h"

#include <esp_dsp.h>
#include <esp_heap_caps.h>
#include <math.h>

namespace listen {

PitchResult yinPitch(const float *x, int n, float sampleRate, float fmin, float fmax, float threshold) {
  PitchResult r;
  int tauMin = (int)(sampleRate / fmax);
  int tauMax = (int)(sampleRate / fmin);
  if (tauMin < 2) tauMin = 2;
  if (tauMax >= n / 2) tauMax = n / 2 - 1;
  if (tauMax <= tauMin) return r;
  const int W = n - tauMax;  // correlation window

  // Scratch in PSRAM, allocated once.
  static float *d = nullptr, *cmnd = nullptr, *prefix = nullptr;
  if (!d) {
    d = (float *)heap_caps_malloc(1024 * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    cmnd = (float *)heap_caps_malloc(1024 * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    prefix = (float *)heap_caps_malloc(1025 * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!d || !cmnd || !prefix) return r;
  }
  if (tauMax >= 1024) return r;

  // Difference function d(tau) = sum (x[i] - x[i+tau])^2
  //                             = E(0..W) + E(tau..tau+W) - 2 * dot(x, x+tau)
  // with E from a prefix sum of squares and the dot product in ESP-DSP assembly.
  prefix[0] = 0;
  for (int i = 0; i < n; i++) prefix[i + 1] = prefix[i] + x[i] * x[i];
  float ea = prefix[W];
  for (int tau = tauMin; tau <= tauMax; tau++) {
    float dot = 0;
    dsps_dotprod_f32(x, x + tau, &dot, W);
    float v = ea + (prefix[tau + W] - prefix[tau]) - 2.0f * dot;
    d[tau] = v > 0 ? v : 0;
  }
  // Cumulative mean normalised difference, computed over the searched range.
  float running = 0;
  int count = 0;
  for (int tau = tauMin; tau <= tauMax; tau++) {
    running += d[tau];
    count++;
    cmnd[tau] = d[tau] * count / (running > 0 ? running : 1e-9f);
  }
  // Absolute threshold: first dip below it, then walk to its local minimum.
  int best = -1;
  for (int tau = tauMin + 1; tau <= tauMax; tau++) {
    if (cmnd[tau] < threshold) {
      while (tau + 1 <= tauMax && cmnd[tau + 1] < cmnd[tau]) tau++;
      best = tau;
      break;
    }
  }
  if (best < 0) {  // no dip under the threshold: take the global minimum if it is decent
    float m = 1e9f;
    for (int tau = tauMin + 1; tau < tauMax; tau++)
      if (cmnd[tau] < m) {
        m = cmnd[tau];
        best = tau;
      }
    if (best < 0 || m > 0.5f) return r;
  }
  // Parabolic interpolation around the minimum.
  float tauF = best;
  if (best > tauMin && best < tauMax) {
    float s0 = cmnd[best - 1], s1 = cmnd[best], s2 = cmnd[best + 1];
    float denom = 2 * (2 * s1 - s2 - s0);
    if (fabsf(denom) > 1e-9f) tauF += (s2 - s0) / denom;
  }
  r.hz = sampleRate / tauF;
  r.confidence = 1.0f - cmnd[best];
  if (r.confidence < 0) r.confidence = 0;
  return r;
}

}  // namespace listen
