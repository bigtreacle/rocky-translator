// Pixel sink. The UI layer owns LVGL; the board owns the panel.
#pragma once
#include <stdint.h>

namespace hal {

class Display {
 public:
  virtual ~Display() = default;
  virtual bool begin() = 0;
  virtual int width() const = 0;
  virtual int height() const = 0;
  // RGB565 with the byte order the panel wants (big-endian on the AMOLED boards).
  // The area is inclusive and already rounded to the panel's alignment rules by the caller.
  virtual void flush(int x1, int y1, int x2, int y2, const uint16_t *pixels) = 0;
  virtual void setBrightness(uint8_t percent) = 0;  // 0 turns the panel off visually; see setPower for real off
  virtual void setPower(bool on) = 0;               // panel sleep in/out
};

}  // namespace hal
