// Colours, fonts and geometry shared by the UI. One accent colour on black
// (AMOLED power and the look); everything else is a dimmer version of it.
#pragma once
#include <lvgl.h>

namespace ui::theme {

constexpr uint32_t ACCENT = 0x39E5B6;
constexpr uint32_t ACCENT_DIM = 0x1F7D64;   // ~55 % of accent, secondary text
constexpr uint32_t ACCENT_GLOW = 0x0E4A3A;  // behind the spectrum bars
constexpr uint32_t TEXT = 0xE6FFF7;         // translation text, slightly cool white
constexpr uint32_t AMBER = 0xFFB000;        // notes heard from the user
constexpr uint32_t ERROR = 0xFF4D4D;

// Panel geometry measured on the real board (DESIGN.md §9): straight edges
// fully visible, corners rounded with a radius of roughly 55 to 80 px.
constexpr int W = 368;
constexpr int H = 448;
constexpr int ROOT_INSET = 6;     // the whole tree lives in a container inset this much so it can shift for burn-in
constexpr int CORNER_INSET = 72;  // nothing readable within this of a side in the top/bottom 30 px
constexpr int SIDE_MARGIN = 24;   // translation text side margin, clears the bottom corners

// Rows, in root-container coordinates (subtract ROOT_INSET from the design's screen rows).
constexpr int STATUS_Y = 0;
constexpr int STATUS_H = 28;
constexpr int SPECTRUM_Y = 30;
constexpr int SPECTRUM_H = 200;
constexpr int NOTE_Y = 242;
constexpr int NOTE_H = 40;
constexpr int TEXT_Y = 294;
constexpr int TEXT_H = 118;  // 3 lines of Montserrat 24 plus cursor room

constexpr int BANDS = 24;

inline const lv_font_t *fontStatus() { return &lv_font_montserrat_14; }
inline const lv_font_t *fontNote() { return &lv_font_montserrat_28; }
inline const lv_font_t *fontText() { return &lv_font_montserrat_24; }

}  // namespace ui::theme
