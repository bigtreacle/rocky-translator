#include "translator_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "spectrum_widget.h"
#include "theme.h"

namespace ui::translator {
namespace {

lv_obj_t *g_root = nullptr;
lv_obj_t *g_dot = nullptr;
lv_obj_t *g_status = nullptr;
lv_obj_t *g_battery = nullptr;
lv_obj_t *g_spectrum = nullptr;
lv_obj_t *g_note = nullptr;
lv_obj_t *g_textBox = nullptr;
lv_obj_t *g_text = nullptr;
lv_obj_t *g_cursor = nullptr;
lv_timer_t *g_typeTimer = nullptr;
lv_timer_t *g_blinkTimer = nullptr;
void (*g_tap)() = nullptr;
void (*g_long)() = nullptr;

char g_full[512];
size_t g_typed = 0;
bool g_typing = false;
Status g_lastStatus = Status::Standby;
int g_lastBattery = -2;
bool g_lastCharging = false;
bool g_lastError = false;
bool g_lastMuted = false;
char g_lastNote[32] = "?";
bool g_lastHeard = false;

void placeCursor() {
  size_t n = strlen(lv_label_get_text(g_text));
  lv_point_t p;
  lv_label_get_letter_pos(g_text, (uint32_t)n, &p);
  lv_obj_set_pos(g_cursor, p.x + 2, p.y + 2);
  // Keep the last line in view once the text runs past three lines.
  int textH = lv_obj_get_height(g_text);
  int boxH = lv_obj_get_height(g_textBox);
  if (textH > boxH) lv_obj_scroll_to_y(g_textBox, textH - boxH, LV_ANIM_OFF);
}

void typeTick(lv_timer_t *) {
  if (!g_typing) return;
  if (g_typed >= strlen(g_full)) {
    g_typing = false;
    return;
  }
  // Append one UTF-8 character (the phrase files are ASCII, but be safe).
  size_t len = 1;
  unsigned char c = (unsigned char)g_full[g_typed];
  if (c >= 0xF0) len = 4;
  else if (c >= 0xE0) len = 3;
  else if (c >= 0xC0) len = 2;
  char shown[512];
  size_t n = g_typed + len;
  memcpy(shown, g_full, n);
  shown[n] = 0;
  g_typed = n;
  lv_label_set_text(g_text, shown);
  lv_obj_update_layout(g_text);
  placeCursor();
}

void blinkTick(lv_timer_t *) {
  static bool on = true;
  on = !on;
  // Solid while typing, blinking when idle with text, hidden when there is no text.
  bool hasText = lv_label_get_text(g_text)[0] != 0;
  lv_obj_set_hidden(g_cursor, !hasText || (!g_typing && !on));
}

void tapCb(lv_event_t *) {
  if (g_tap) g_tap();
}
void longCb(lv_event_t *) {
  if (g_long) g_long();
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, "");
  return l;
}

}  // namespace

void create() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);
  // React on finger-down, not on release: immediate, and a finger that slides a
  // little still counts as a tap.
  lv_obj_add_event_cb(scr, tapCb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(scr, longCb, LV_EVENT_LONG_PRESSED, nullptr);

  const int W = theme::W - 2 * theme::ROOT_INSET;
  const int H = theme::H - 2 * theme::ROOT_INSET;

  g_root = lv_obj_create(scr);
  lv_obj_remove_style_all(g_root);
  lv_obj_set_pos(g_root, theme::ROOT_INSET, theme::ROOT_INSET);
  lv_obj_set_size(g_root, W, H);
  lv_obj_set_scrollable(g_root, false);
  lv_obj_add_event_cb(g_root, tapCb, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(g_root, longCb, LV_EVENT_LONG_PRESSED, nullptr);

  // Status strip: dot, word, battery. Inset from the sides to clear the rounded corners.
  int left = theme::CORNER_INSET - theme::ROOT_INSET;
  int right = W - (theme::CORNER_INSET - theme::ROOT_INSET);
  g_dot = lv_obj_create(g_root);
  lv_obj_remove_style_all(g_dot);
  lv_obj_set_size(g_dot, 8, 8);
  lv_obj_set_pos(g_dot, left, theme::STATUS_Y + 10);
  lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(g_dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(g_dot, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_clickable(g_dot, false);

  g_status = label(g_root, theme::fontStatus(), theme::ACCENT);
  lv_obj_set_pos(g_status, left + 16, theme::STATUS_Y + 6);
  lv_obj_set_style_text_letter_space(g_status, 2, 0);

  g_battery = label(g_root, theme::fontStatus(), theme::ACCENT_DIM);
  lv_obj_set_width(g_battery, 90);
  lv_obj_set_style_text_align(g_battery, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(g_battery, right - 90, theme::STATUS_Y + 6);

  g_spectrum = spectrumCreate(g_root, 0, theme::SPECTRUM_Y, W, theme::SPECTRUM_H);

  g_note = label(g_root, theme::fontNote(), theme::ACCENT);
  lv_obj_set_width(g_note, W);
  lv_obj_set_style_text_align(g_note, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(g_note, 0, theme::NOTE_Y + 4);
  lv_obj_set_style_text_letter_space(g_note, 3, 0);

  // Translation box: a clipping container so long text can scroll up a line.
  int margin = theme::SIDE_MARGIN - theme::ROOT_INSET;
  g_textBox = lv_obj_create(g_root);
  lv_obj_remove_style_all(g_textBox);
  lv_obj_set_pos(g_textBox, margin, theme::TEXT_Y);
  lv_obj_set_size(g_textBox, W - 2 * margin, theme::TEXT_H);
  lv_obj_set_clickable(g_textBox, false);
  lv_obj_set_scrollable(g_textBox, false);  // we scroll it ourselves
  lv_obj_set_style_clip_corner(g_textBox, true, 0);

  g_text = label(g_textBox, theme::fontText(), theme::TEXT);
  lv_obj_set_width(g_text, W - 2 * margin - 12);
  lv_label_set_long_mode(g_text, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_line_space(g_text, 6, 0);
  lv_obj_set_pos(g_text, 0, 0);

  g_cursor = lv_obj_create(g_textBox);
  lv_obj_remove_style_all(g_cursor);
  lv_obj_set_size(g_cursor, 3, 24);
  lv_obj_set_style_bg_opa(g_cursor, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(g_cursor, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_clickable(g_cursor, false);
  lv_obj_set_hidden(g_cursor, true);

  g_typeTimer = lv_timer_create(typeTick, 33, nullptr);   // ~30 chars/s
  g_blinkTimer = lv_timer_create(blinkTick, 500, nullptr);
}

void apply(const UiModel &m) {
  if (!g_root) return;
  spectrumSetLevels(g_spectrum, m.bands);

  if (m.status != g_lastStatus) {
    g_lastStatus = m.status;
    lv_label_set_text(g_status, statusWord(m.status));
  }
  if (m.error != g_lastError) {
    g_lastError = m.error;
    lv_obj_set_style_bg_color(g_dot, lv_color_hex(m.error ? theme::ERROR : theme::ACCENT), 0);
  }
  if (m.batteryPercent != g_lastBattery || m.charging != g_lastCharging || m.muted != g_lastMuted) {
    g_lastBattery = m.batteryPercent;
    g_lastCharging = m.charging;
    g_lastMuted = m.muted;
    char buf[48];
    if (m.batteryPercent < 0) {
      snprintf(buf, sizeof buf, "%s", m.charging ? LV_SYMBOL_USB : "");
    } else {
      const char *sym = m.batteryPercent > 85   ? LV_SYMBOL_BATTERY_FULL
                        : m.batteryPercent > 60 ? LV_SYMBOL_BATTERY_3
                        : m.batteryPercent > 35 ? LV_SYMBOL_BATTERY_2
                        : m.batteryPercent > 10 ? LV_SYMBOL_BATTERY_1
                                                : LV_SYMBOL_BATTERY_EMPTY;
      snprintf(buf, sizeof buf, "%s%s %d%%", m.charging ? LV_SYMBOL_CHARGE " " : "", sym, m.batteryPercent);
    }
    if (m.muted) {
      char withMute[64];
      snprintf(withMute, sizeof withMute, LV_SYMBOL_MUTE "  %s", buf);
      lv_label_set_text(g_battery, withMute);
    } else {
      lv_label_set_text(g_battery, buf);
    }
  }
  if (strcmp(m.note, g_lastNote) != 0 || m.noteIsHeard != g_lastHeard) {
    strncpy(g_lastNote, m.note, sizeof g_lastNote - 1);
    g_lastHeard = m.noteIsHeard;
    lv_label_set_text(g_note, m.note);
    lv_obj_set_style_text_color(g_note, lv_color_hex(m.noteIsHeard ? theme::AMBER : theme::ACCENT), 0);
  }
}

void startTyping(const char *text) {
  strncpy(g_full, text, sizeof g_full - 1);
  g_full[sizeof g_full - 1] = 0;
  g_typed = 0;
  g_typing = true;
  lv_label_set_text(g_text, "");
  lv_obj_scroll_to_y(g_textBox, 0, LV_ANIM_OFF);
  lv_obj_update_layout(g_text);
  placeCursor();
  lv_obj_set_hidden(g_cursor, false);
}

bool typingDone() { return !g_typing; }

void shiftForBurnIn() {
  int dx = (int)lv_rand(0, 8) - 4;
  int dy = (int)lv_rand(0, 8) - 4;
  lv_obj_set_pos(g_root, theme::ROOT_INSET + dx, theme::ROOT_INSET + dy);
}

void setTapHandler(void (*fn)()) { g_tap = fn; }
void setLongPressHandler(void (*fn)()) { g_long = fn; }

void setVisible(bool on) {
  if (g_root) lv_obj_set_hidden(g_root, !on);
}

}  // namespace ui::translator
