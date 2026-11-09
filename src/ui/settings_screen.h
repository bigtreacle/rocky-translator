// Settings (DESIGN.md §12), opened by a long press. A scrollable list of
// sliders and switches over the translator; closes on Back or after 20 s idle.
#pragma once
#include <stdint.h>

namespace ui::settings {

// Callbacks the settings screen drives. Values are applied live by the caller.
struct Hooks {
  void (*setVolume)(uint8_t) = nullptr;
  void (*setMuted)(bool) = nullptr;
  void (*setBassCut)(bool) = nullptr;
  void (*setSensitivity)(int) = nullptr;
  void (*setListening)(bool) = nullptr;
  void (*setIdleChatter)(bool) = nullptr;
  void (*setMirrorReply)(bool) = nullptr;
  void (*setBrightness)(uint8_t) = nullptr;
  void (*checkUpdates)() = nullptr;  // may be null: the row is then hidden
  void (*wifiSetup)() = nullptr;     // may be null
};

struct Values {
  uint8_t volume = 60;
  bool muted = false;
  bool bassCut = true;
  int sensitivity = 10;
  bool listening = true;
  bool idleChatter = false;
  bool mirrorReply = true;
  uint8_t brightness = 100;
  const char *version = "dev";
  const char *wifi = "";  // status line under the Wi-Fi row
};

void create(const Hooks &hooks);
void open(const Values &v);
void close();
bool isOpen();
void tick(uint32_t nowMs);  // auto-close
void setStatus(const char *text);  // e.g. update progress, shown at the top

}  // namespace ui::settings
