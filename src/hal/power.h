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

class Power {
 public:
  virtual ~Power() = default;
  virtual bool begin() = 0;
  virtual PowerState read() = 0;
  virtual bool buttonPressed() = 0;  // power button held right now
  virtual void shutdown() = 0;       // cut power via the PMIC
};

}  // namespace hal
