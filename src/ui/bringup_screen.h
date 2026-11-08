// M0 test screen: colour bars, an edge frame, an odd-aligned rectangle (exercises
// the even-pixel rounder), identity text, a live status block and a touch dot.
#pragma once

namespace ui {

void bringupCreate(const char *title, const char *subtitle);
void bringupSetStatus(const char *text);  // multi-line, bottom of the screen
void bringupSetTouch(bool pressed, int x, int y);

}  // namespace ui
