#pragma once

namespace hal {

struct Accel {
  float x = 0, y = 0, z = 0;  // g
};

// Raw samples only. Pick-up / shake / face-down detection lives in src/motion/,
// which is board-independent.
class Motion {
 public:
  virtual ~Motion() = default;
  virtual bool begin() = 0;
  virtual bool read(Accel &out) = 0;  // false if no new sample
};

}  // namespace hal
