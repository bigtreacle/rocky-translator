// The translator console: status strip, spectrum, note line, typed translation.
// The brain drives it through apply() each frame and startTyping() when a
// phrase has been spoken. Only this module and its siblings touch LVGL.
#pragma once
#include "ui_model.h"

namespace ui::translator {

void create();                         // builds the screen and loads it
void apply(const UiModel &m);          // per frame
void startTyping(const char *text);    // types text out character by character
bool typingDone();
void shiftForBurnIn();                 // nudge the whole tree a few pixels
void setTapHandler(void (*fn)());      // a tap anywhere on the screen

}  // namespace ui::translator
