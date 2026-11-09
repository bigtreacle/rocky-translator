// Entry point to the hardware layer. Exactly one src/boards/<name>/ is compiled
// per PlatformIO environment and provides these.
#pragma once
#include <stdint.h>

#include "audio_in.h"
#include "audio_out.h"
#include "display.h"
#include "motion.h"
#include "power.h"
#include "touch.h"

namespace hal {

struct BoardInfo {
  const char *name = "";
  const char *revision = "";  // e.g. "V2 (CO5300 + CST820)"
  int sda = -1, scl = -1;     // the I2C pins that answered
  uint8_t i2cFound[16] = {};
  int i2cFoundCount = 0;
};

// Brings up buses, power rails and reset lines, and detects the board revision.
// Call once before any device's begin(). Returns false if the board does not
// answer at all (no I2C devices).
bool boardBegin(BoardInfo &info);

bool bootButtonPressed();  // the BOOT side button, held right now

enum class WakeCause : uint8_t { Timer, Touch, Button, Other };
// Light sleep for up to `ms`, waking early on a touch or the BOOT button. The
// caller has already turned the screen off and paused audio. Returns why we woke.
WakeCause lightSleep(uint32_t ms);
void debugAudio(Print &out);  // amp pin read-back and codec registers, for the serial console
bool debugCodecWrite(uint8_t reg, uint8_t value);  // poke an ES8311 register (diagnostics only)
void debugRails(Print &out);  // PMIC rail states

Display &display();
Touch &touch();
AudioOut &audioOut();
AudioIn &audioIn();
Motion &motion();
Power &power();

}  // namespace hal
