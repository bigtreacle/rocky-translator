#pragma once
#include <stddef.h>
#include <stdint.h>

namespace hal {

class AudioIn {
 public:
  virtual ~AudioIn() = default;
  virtual bool begin(uint32_t sampleRate) = 0;
  // Mono int16 samples; blocks until `frames` are available.
  virtual size_t read(int16_t *monoFrames, size_t frames) = 0;
  virtual void setGainDb(int db) = 0;
};

}  // namespace hal
