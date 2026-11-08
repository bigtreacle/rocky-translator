// What the brain hands the UI each frame. No LVGL types here: the brain fills
// this, the UI draws it.
#pragma once
#include <stdint.h>

namespace ui {

enum class Status : uint8_t { Idle, Listening, Receiving, Translating, Speaking, Standby };

inline const char *statusWord(Status s) {
  switch (s) {
    case Status::Idle: return "IDLE";
    case Status::Listening: return "LISTENING";
    case Status::Receiving: return "RECEIVING";
    case Status::Translating: return "TRANSLATING";
    case Status::Speaking: return "SPEAKING";
    case Status::Standby: return "STANDBY";
  }
  return "";
}

struct UiModel {
  Status status = Status::Idle;
  float bands[24] = {};   // 0..1 per band, already smoothed by the producer
  char note[32] = "";     // "C4 E4 G4", or the pitch heard; empty hides the line
  bool noteIsHeard = false;  // amber (heard from the user) rather than accent (spoken)
  int batteryPercent = -1;   // -1 unknown
  bool charging = false;
  bool error = false;        // red status dot
  bool muted = true;         // speaker muted (shown in the status strip)
};

}  // namespace ui
