// LVGL glue: the only place that connects LVGL to hal::Display and hal::Touch.
#pragma once
#include "../hal/display.h"
#include "../hal/touch.h"

namespace ui {

bool lvglBegin(hal::Display &display, hal::Touch &touch);
void lvglLoop();  // call from loop(); runs LVGL timers

}  // namespace ui
