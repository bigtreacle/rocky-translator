#include "settings_screen.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>

#include "theme.h"

namespace ui::settings {
namespace {

Hooks g_hooks;
lv_obj_t *g_panel = nullptr;
lv_obj_t *g_list = nullptr;
lv_obj_t *g_status = nullptr;
lv_obj_t *g_wifiLabel = nullptr;
uint32_t g_lastTouch = 0;
bool g_open = false;
constexpr uint32_t AUTO_CLOSE_MS = 20000;

struct Row {
  lv_obj_t *control;  // slider or switch
  lv_obj_t *value;    // value label for sliders, may be null
};
Row g_volume, g_sound, g_bass, g_sens, g_listen, g_chatter, g_mirror, g_bright;

void touched(lv_event_t *) { g_lastTouch = millis(); }

lv_obj_t *rowBase(const char *title) {
  lv_obj_t *row = lv_obj_create(g_list);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, lv_pct(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(row, 10, 0);
  lv_obj_set_style_pad_bottom(row, 10, 0);
  lv_obj_set_style_pad_left(row, 4, 0);
  lv_obj_set_style_pad_right(row, 4, 0);
  lv_obj_set_scrollable(row, false);
  lv_obj_t *t = lv_label_create(row);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, theme::fontStatus(), 0);
  lv_obj_set_style_text_color(t, lv_color_hex(theme::TEXT), 0);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
  return row;
}

Row sliderRow(const char *title, int min, int max, void (*cb)(lv_event_t *)) {
  lv_obj_t *row = rowBase(title);
  Row r;
  r.value = lv_label_create(row);
  lv_obj_set_style_text_font(r.value, theme::fontStatus(), 0);
  lv_obj_set_style_text_color(r.value, lv_color_hex(theme::ACCENT), 0);
  lv_obj_align(r.value, LV_ALIGN_TOP_RIGHT, 0, 0);
  r.control = lv_slider_create(row);
  lv_obj_set_width(r.control, lv_pct(100));
  lv_obj_set_height(r.control, 8);
  lv_obj_align(r.control, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_slider_set_range(r.control, min, max);
  lv_obj_set_style_bg_color(r.control, lv_color_hex(theme::ACCENT_GLOW), LV_PART_MAIN);
  lv_obj_set_style_bg_color(r.control, lv_color_hex(theme::ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(r.control, lv_color_hex(theme::ACCENT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(r.control, 6, LV_PART_KNOB);
  lv_obj_add_event_cb(r.control, cb, LV_EVENT_VALUE_CHANGED, r.value);
  lv_obj_add_event_cb(r.control, touched, LV_EVENT_PRESSING, nullptr);
  lv_obj_set_style_pad_bottom(row, 22, 0);
  return r;
}

Row switchRow(const char *title, void (*cb)(lv_event_t *)) {
  lv_obj_t *row = rowBase(title);
  Row r;
  r.value = nullptr;
  r.control = lv_switch_create(row);
  lv_obj_align(r.control, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(r.control, lv_color_hex(theme::ACCENT_GLOW), LV_PART_MAIN);
  lv_obj_set_style_bg_color(r.control, lv_color_hex(theme::ACCENT), (lv_style_selector_t)LV_PART_INDICATOR | (lv_style_selector_t)LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(r.control, lv_color_hex(theme::TEXT), LV_PART_KNOB);
  lv_obj_add_event_cb(r.control, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(r.control, touched, LV_EVENT_CLICKED, nullptr);
  return r;
}

lv_obj_t *buttonRow(const char *title, void (*cb)(lv_event_t *)) {
  lv_obj_t *b = lv_button_create(g_list);
  lv_obj_set_width(b, lv_pct(100));
  lv_obj_set_height(b, 44);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT_GLOW), 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_t *t = lv_label_create(b);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, theme::fontStatus(), 0);
  lv_obj_set_style_text_color(t, lv_color_hex(theme::TEXT), 0);
  lv_obj_center(t);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(b, touched, LV_EVENT_CLICKED, nullptr);
  return b;
}

int sliderVal(lv_event_t *e) { return (int)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e)); }
bool switchVal(lv_event_t *e) { return lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED); }
void setValueLabel(lv_event_t *e, const char *fmt) {
  lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
  if (lbl) lv_label_set_text_fmt(lbl, fmt, sliderVal(e));
}

void onVolume(lv_event_t *e) { setValueLabel(e, "%d"); if (g_hooks.setVolume) g_hooks.setVolume((uint8_t)sliderVal(e)); }
void onSound(lv_event_t *e) { if (g_hooks.setMuted) g_hooks.setMuted(!switchVal(e)); }
void onBass(lv_event_t *e) { if (g_hooks.setBassCut) g_hooks.setBassCut(switchVal(e)); }
void onSens(lv_event_t *e) { setValueLabel(e, "%d dB"); if (g_hooks.setSensitivity) g_hooks.setSensitivity(sliderVal(e)); }
void onListen(lv_event_t *e) { if (g_hooks.setListening) g_hooks.setListening(switchVal(e)); }
void onChatter(lv_event_t *e) { if (g_hooks.setIdleChatter) g_hooks.setIdleChatter(switchVal(e)); }
void onMirror(lv_event_t *e) { if (g_hooks.setMirrorReply) g_hooks.setMirrorReply(switchVal(e)); }
void onBright(lv_event_t *e) { setValueLabel(e, "%d%%"); if (g_hooks.setBrightness) g_hooks.setBrightness((uint8_t)sliderVal(e)); }
void onUpdate(lv_event_t *) { if (g_hooks.checkUpdates) g_hooks.checkUpdates(); }
void onWifi(lv_event_t *) { if (g_hooks.wifiSetup) g_hooks.wifiSetup(); }
void onBack(lv_event_t *) { close(); }

void setSwitch(Row &r, bool on) {
  if (on) lv_obj_add_state(r.control, LV_STATE_CHECKED);
  else lv_obj_remove_state(r.control, LV_STATE_CHECKED);
}
void setSlider(Row &r, int v, const char *fmt) {
  lv_slider_set_value(r.control, v, LV_ANIM_OFF);
  if (r.value) lv_label_set_text_fmt(r.value, fmt, v);
}

}  // namespace

void create(const Hooks &hooks) {
  g_hooks = hooks;
  lv_obj_t *scr = lv_screen_active();
  g_panel = lv_obj_create(scr);
  lv_obj_remove_style_all(g_panel);
  lv_obj_set_pos(g_panel, 0, 0);
  lv_obj_set_size(g_panel, theme::W, theme::H);
  lv_obj_set_style_bg_color(g_panel, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_panel, LV_OPA_COVER, 0);
  lv_obj_set_scrollable(g_panel, false);
  lv_obj_add_event_cb(g_panel, touched, LV_EVENT_PRESSED, nullptr);

  lv_obj_t *title = lv_label_create(g_panel);
  lv_label_set_text(title, "SETTINGS");
  lv_obj_set_style_text_font(title, theme::fontStatus(), 0);
  lv_obj_set_style_text_color(title, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_text_letter_space(title, 2, 0);
  lv_obj_set_pos(title, theme::CORNER_INSET, 12);

  g_status = lv_label_create(g_panel);
  lv_label_set_text(g_status, "");
  lv_obj_set_style_text_font(g_status, theme::fontStatus(), 0);
  lv_obj_set_style_text_color(g_status, lv_color_hex(theme::ACCENT_DIM), 0);
  lv_obj_set_width(g_status, theme::W - 2 * theme::CORNER_INSET);
  lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(g_status, theme::CORNER_INSET, 12);

  // The list sits between the rounded corners vertically and uses the full
  // side margin: the top and bottom 30 px are kept clear of the corners.
  g_list = lv_obj_create(g_panel);
  lv_obj_remove_style_all(g_list);
  lv_obj_set_pos(g_list, theme::SIDE_MARGIN, 40);
  lv_obj_set_size(g_list, theme::W - 2 * theme::SIDE_MARGIN, theme::H - 40 - 36);
  lv_obj_set_flex_flow(g_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(g_list, 2, 0);
  lv_obj_set_scroll_dir(g_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(g_list, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(g_list, touched, LV_EVENT_SCROLL, nullptr);

  g_sound = switchRow("Sound", onSound);
  g_volume = sliderRow("Volume", 0, 100, onVolume);
  g_bass = switchRow("Bass cut", onBass);
  g_listen = switchRow("Listening", onListen);
  g_sens = sliderRow("Mic sensitivity", 6, 24, onSens);
  g_mirror = switchRow("Sing back whistles", onMirror);
  g_chatter = switchRow("Idle chatter", onChatter);
  g_bright = sliderRow("Brightness", 20, 100, onBright);
  if (g_hooks.wifiSetup) {
    buttonRow("Wi-Fi setup", onWifi);
    g_wifiLabel = lv_label_create(g_list);
    lv_label_set_text(g_wifiLabel, "");
    lv_obj_set_style_text_font(g_wifiLabel, theme::fontStatus(), 0);
    lv_obj_set_style_text_color(g_wifiLabel, lv_color_hex(theme::ACCENT_DIM), 0);
  }
  if (g_hooks.checkUpdates) buttonRow("Check for updates", onUpdate);
  buttonRow("Back", onBack);

  lv_obj_set_hidden(g_panel, true);
}

void open(const Values &v) {
  if (!g_panel) return;
  setSwitch(g_sound, !v.muted);
  setSlider(g_volume, v.volume, "%d");
  setSwitch(g_bass, v.bassCut);
  setSwitch(g_listen, v.listening);
  setSlider(g_sens, v.sensitivity, "%d dB");
  setSwitch(g_mirror, v.mirrorReply);
  setSwitch(g_chatter, v.idleChatter);
  setSlider(g_bright, v.brightness, "%d%%");
  if (g_wifiLabel) lv_label_set_text(g_wifiLabel, v.wifi);
  lv_label_set_text(g_status, v.version);
  lv_obj_scroll_to_y(g_list, 0, LV_ANIM_OFF);
  lv_obj_set_hidden(g_panel, false);
  lv_obj_move_foreground(g_panel);
  g_open = true;
  g_lastTouch = millis();
}

void close() {
  if (!g_panel) return;
  lv_obj_set_hidden(g_panel, true);
  g_open = false;
}

bool isOpen() { return g_open; }

void tick(uint32_t now) {
  if (g_open && now - g_lastTouch > AUTO_CLOSE_MS) close();
}

void setStatus(const char *text) {
  if (g_status) lv_label_set_text(g_status, text);
}

}  // namespace ui::settings
