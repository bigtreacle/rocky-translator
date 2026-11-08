// YIN pitch estimation (de Cheveigné & Kawahara 2002) on a block of mono
// samples. Returns the fundamental in Hz, or 0 if none is confident enough.
#pragma once

namespace listen {

struct PitchResult {
  float hz = 0;          // 0 = no pitch
  float confidence = 0;  // 1 - normalised difference at the chosen lag, 0..1
};

// x: n samples. Searches fmin..fmax. threshold ~0.15 is the usual choice.
PitchResult yinPitch(const float *x, int n, float sampleRate, float fmin, float fmax, float threshold);

}  // namespace listen
