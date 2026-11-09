// Gestures from raw accelerometer samples (DESIGN.md §11). Board-independent:
// feed it hal::Accel samples at roughly 50 Hz, poll events.
//
// Orientation on this board, measured: lying screen-up reads z = -1 g.
#pragma once
#include <stdint.h>

#include "../hal/motion.h"

namespace motion {

enum class Gesture : uint8_t { None, PickedUp, Shaken, FaceDown, FaceUp };

void begin();
Gesture update(const hal::Accel &a, uint32_t nowMs);  // one sample in, at most one gesture out

// Diagnostics
struct Debug { float mag, hpMag, magVar; bool still; int hits; uint32_t stillMs; };
Debug debug(uint32_t nowMs);
bool isStill();
bool isFaceDown();
float magnitude();  // |a| in g, last sample

}  // namespace motion
