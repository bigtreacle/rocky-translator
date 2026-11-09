#pragma once
#include <stdint.h>

namespace hal {

struct PowerState {
  bool batteryPresent = false;
  bool charging = false;
  bool usbPresent = false;
  uint16_t batteryMillivolts = 0;
  int batteryPercent = -1;  // -1 when unknown
};

enum class ButtonEvent : uint8_t { None, Short, Long };

class Power {
 public:
  virtual ~Power() = default;
  virtual bool begin() = 0;
  virtual PowerState read() = 0;
  virtual bool buttonPressed() = 0;     // power button held right now
  virtual ButtonEvent pollButton() = 0;  // PMIC power-key events since the last call
  virtual void shutdown() = 0;          // cut power via the PMIC
};

}  // namespace hal
