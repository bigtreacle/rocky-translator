#include "bringup_screen.h"

#include <lvgl.h>

namespace ui {
namespace {

lv_obj_t *g_status = nullptr;
lv_obj_t *g_dot = nullptr;

lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t color) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, color, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  return o;
}

}  // namespace

void bringupCreate(const char *title, const char *subtitle) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(scr, false);

  int w = lv_obj_get_width(scr);
  int h = lv_obj_get_height(scr);

  // Colour bars: red, green, blue, white. Wrong colour order or swapped bytes shows here.
  lv_color_t bars[4] = {lv_color_make(255, 0, 0), lv_color_make(0, 255, 0), lv_color_make(0, 0, 255),
                        lv_color_white()};
  int bw = w / 4;
  for (int i = 0; i < 4; i++) rect(scr, i * bw, 0, bw, 40, bars[i]);

  // 3 px frame at the very edge. On the real board a 1 px frame was invisible,
  // so the bezel hides at least the outermost pixel; this shows how much.
  lv_obj_t *frame = lv_obj_create(scr);
  lv_obj_remove_style_all(frame);
  lv_obj_set_pos(frame, 0, 0);
  lv_obj_set_size(frame, w, h);
  lv_obj_set_style_border_color(frame, lv_color_white(), 0);
  lv_obj_set_style_border_width(frame, 3, 0);
  lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
  lv_obj_set_clickable(frame, false);

  // Corner guides: a 6 px dot every 8 px along the diagonal out of each corner,
  // six per corner. The bezel is rounded, so counting the visible dots gives the
  // corner inset: first visible dot n (1-based) means roughly 8*n px is safe.
  for (int i = 1; i <= 6; i++) {
    int d = 8 * i;
    rect(scr, d - 3, d - 3, 6, 6, lv_color_white());
    rect(scr, w - d - 3, d - 3, 6, 6, lv_color_white());
    rect(scr, d - 3, h - d - 3, 6, 6, lv_color_white());
    rect(scr, w - d - 3, h - d - 3, 6, 6, lv_color_white());
  }

  // Deliberately odd-aligned rectangle: garbage at its edges means the rounder is not working.
  rect(scr, 33, 61, 51, 21, lv_color_hex(0x39E5B6));

  lv_obj_t *t = lv_label_create(scr);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, lv_color_hex(0x39E5B6), 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);
  lv_obj_set_pos(t, 20, 100);

  lv_obj_t *s = lv_label_create(scr);
  lv_label_set_text(s, subtitle);
  lv_obj_set_style_text_color(s, lv_color_hex(0xB0B0B0), 0);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_14, 0);
  lv_obj_set_width(s, w - 40);
  lv_label_set_long_mode(s, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(s, 20, 140);

  g_status = lv_label_create(scr);
  lv_label_set_text(g_status, "");
  lv_obj_set_style_text_color(g_status, lv_color_white(), 0);
  lv_obj_set_style_text_font(g_status, &lv_font_montserrat_14, 0);
  lv_obj_set_width(g_status, w - 40);
  lv_label_set_long_mode(g_status, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(g_status, 20, 230);

  g_dot = rect(scr, 0, 0, 24, 24, lv_color_hex(0xFFB000));
  lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_hidden(g_dot, true);
}

void bringupSetStatus(const char *text) {
  if (g_status) lv_label_set_text(g_status, text);
}

void bringupSetTouch(bool pressed, int x, int y) {
  if (!g_dot) return;
  if (pressed) {
    lv_obj_set_pos(g_dot, x - 12, y - 12);
    lv_obj_set_hidden(g_dot, false);
  } else {
    lv_obj_set_hidden(g_dot, true);
  }
}

}  // namespace ui
