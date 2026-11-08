#include "spectrum_widget.h"

#include <string.h>

#include "theme.h"

namespace ui {
namespace {

struct SpectrumData {
  float levels[theme::BANDS];
};

constexpr int BAR_W = 10;
constexpr int GAP = 4;
constexpr int GLOW = 3;  // px the glow extends beyond the bar on each side

void drawCb(lv_event_t *e) {
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
  auto *d = (SpectrumData *)lv_obj_get_user_data(obj);
  if (!d) return;
  lv_layer_t *layer = lv_event_get_layer(e);

  lv_area_t coords;
  lv_obj_get_coords(obj, &coords);
  int h = lv_area_get_height(&coords);
  int total = theme::BANDS * BAR_W + (theme::BANDS - 1) * GAP;
  int x0 = coords.x1 + (lv_area_get_width(&coords) - total) / 2;
  int baseline = coords.y2 - GLOW;

  lv_draw_rect_dsc_t glow;
  lv_draw_rect_dsc_init(&glow);
  glow.bg_color = lv_color_hex(theme::ACCENT);
  glow.bg_opa = LV_OPA_30;
  glow.radius = 5;

  lv_draw_rect_dsc_t bar;
  lv_draw_rect_dsc_init(&bar);
  bar.bg_color = lv_color_hex(theme::ACCENT);
  bar.bg_opa = LV_OPA_COVER;
  bar.radius = 3;

  for (int i = 0; i < theme::BANDS; i++) {
    float v = d->levels[i];
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    int bh = 3 + (int)(v * (h - 2 * GLOW - 3));
    int x = x0 + i * (BAR_W + GAP);
    lv_area_t a = {(int32_t)x, (int32_t)(baseline - bh), (int32_t)(x + BAR_W - 1), (int32_t)baseline};
    lv_area_t g = {a.x1 - GLOW, a.y1 - GLOW, a.x2 + GLOW, a.y2 + GLOW};
    lv_draw_rect(layer, &glow, &g);
    lv_draw_rect(layer, &bar, &a);
  }
}

void deleteCb(lv_event_t *e) {
  lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
  delete (SpectrumData *)lv_obj_get_user_data(obj);
  lv_obj_set_user_data(obj, nullptr);
}

}  // namespace

lv_obj_t *spectrumCreate(lv_obj_t *parent, int x, int y, int w, int h) {
  lv_obj_t *obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_clickable(obj, false);
  lv_obj_set_scrollable(obj, false);
  auto *d = new SpectrumData();
  memset(d->levels, 0, sizeof d->levels);
  lv_obj_set_user_data(obj, d);
  lv_obj_add_event_cb(obj, drawCb, LV_EVENT_DRAW_MAIN, nullptr);
  lv_obj_add_event_cb(obj, deleteCb, LV_EVENT_DELETE, nullptr);
  return obj;
}

void spectrumSetLevels(lv_obj_t *spectrum, const float *levels24) {
  auto *d = (SpectrumData *)lv_obj_get_user_data(spectrum);
  if (!d) return;
  bool changed = false;
  for (int i = 0; i < theme::BANDS; i++) {
    if (d->levels[i] != levels24[i]) {
      d->levels[i] = levels24[i];
      changed = true;
    }
  }
  if (changed) lv_obj_invalidate(spectrum);
}

}  // namespace ui
