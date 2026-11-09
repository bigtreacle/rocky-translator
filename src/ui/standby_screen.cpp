#include "standby_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <math.h>

#include "theme.h"

namespace ui::standby {
namespace {
lv_obj_t *g_cover = nullptr;
lv_obj_t *g_dot = nullptr;
bool g_on = false;
}  // namespace

void create() {
  lv_obj_t *scr = lv_screen_active();
  g_cover = lv_obj_create(scr);
  lv_obj_remove_style_all(g_cover);
  lv_obj_set_pos(g_cover, 0, 0);
  lv_obj_set_size(g_cover, theme::W, theme::H);
  lv_obj_set_style_bg_color(g_cover, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_cover, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(g_cover, false);
  lv_obj_set_clickable(g_cover, false);  // taps fall through to the screen's handler
  g_dot = lv_obj_create(g_cover);
  lv_obj_remove_style_all(g_dot);
  lv_obj_set_size(g_dot, 6, 6);
  lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_dot, lv_color_hex(theme::ACCENT_DIM), 0);
  lv_obj_set_style_bg_opa(g_dot, LV_OPA_COVER, 0);
  lv_obj_set_clickable(g_dot, false);
  lv_obj_set_hidden(g_cover, true);
}

void show(bool on) {
  g_on = on;
  if (g_cover) lv_obj_set_hidden(g_cover, !on);
  if (on) lv_obj_move_foreground(g_cover);
}

void tick() {
  if (!g_on || !g_dot) return;
  // Lissajous path, well inside the rounded corners, one lap every ~40 s.
  float t = millis() / 1000.0f;
  int cx = theme::W / 2, cy = theme::H / 2;
  int x = cx + (int)((theme::W / 2 - 60) * sinf(t * 0.157f));
  int y = cy + (int)((theme::H / 2 - 70) * sinf(t * 0.113f + 1.0f));
  lv_obj_set_pos(g_dot, x - 3, y - 3);
}

}  // namespace ui::standby
