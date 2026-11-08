#pragma once
#include <stdint.h>

namespace hal {

struct TouchPoint {
  bool pressed = false;
  int16_t x = 0;  // screen pixels, same orientation as Display
  int16_t y = 0;
};

class Touch {
 public:
  virtual ~Touch() = default;
  virtual bool begin() = 0;
  virtual TouchPoint read() = 0;
  virtual const char *chipName() const = 0;
};

}  // namespace hal
