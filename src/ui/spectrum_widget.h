// 24 glowing bars drawn in one custom object: one invalidation per frame,
// no child objects.
#pragma once
#include <lvgl.h>

namespace ui {

lv_obj_t *spectrumCreate(lv_obj_t *parent, int x, int y, int w, int h);
void spectrumSetLevels(lv_obj_t *spectrum, const float *levels24);

}  // namespace ui
