#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hal {

class AudioOut {
 public:
  virtual ~AudioOut() = default;
  virtual bool begin(uint32_t sampleRate) = 0;
  // Interleaved stereo int16 frames; blocks until the DMA has taken them.
  virtual size_t write(const int16_t *stereoFrames, size_t frames) = 0;
  virtual void setVolume(uint8_t percent) = 0;
  virtual void setAmpEnabled(bool on) = 0;
};

}  // namespace hal
