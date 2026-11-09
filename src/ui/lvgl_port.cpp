#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

namespace ui {
namespace {

hal::Display *g_display = nullptr;
hal::Touch *g_touch = nullptr;
constexpr int BUF_LINES = 40;  // two partial buffers of 40 lines, 29 KB each at 368 px

// The SH8601 and CO5300 reject windows that do not start on an even pixel and
// end on an odd one. Round every invalidated area outward.
void rounderCb(lv_event_t *e) {
  lv_area_t *a = lv_event_get_invalidated_area(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

void flushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  lv_draw_sw_rgb565_swap(px_map, w * h);  // panel wants big-endian RGB565
  g_display->flush(area->x1, area->y1, area->x2, area->y2, reinterpret_cast<const uint16_t *>(px_map));
  lv_display_flush_ready(disp);
}

void touchReadCb(lv_indev_t *, lv_indev_data_t *data) {
  hal::TouchPoint p = g_touch->read();
  data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (p.pressed) {
    data->point.x = p.x;
    data->point.y = p.y;
  }
}

}  // namespace

bool lvglBegin(hal::Display &display, hal::Touch &touch) {
  g_display = &display;
  g_touch = &touch;
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });

  lv_display_t *disp = lv_display_create(display.width(), display.height());
  size_t bufBytes = (size_t)display.width() * BUF_LINES * sizeof(uint16_t);
  // Internal, DMA-capable RAM: Arduino_GFX hands this pointer straight to the SPI DMA.
  void *buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  void *buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!buf1 || !buf2) {
    Serial.println("lvgl: could not allocate draw buffers");
    return false;
  }
  lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_flush_cb(disp, flushCb);
  lv_display_add_event_cb(disp, rounderCb, LV_EVENT_INVALIDATE_AREA, nullptr);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchReadCb);
  lv_indev_set_long_press_time(indev, 800);
  return true;
}

void lvglLoop() { lv_timer_handler(); }

}  // namespace ui
