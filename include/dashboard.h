// Render helpers for the homelab dashboard.
//
// Everything here is pure black/white: state is shown by inversion, borders and
// glyphs, never by grey or color. Each setter skips work when nothing changed,
// because any LVGL invalidation turns into an e-paper refresh.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "lvgl.h"

namespace dash {

// MDI glyphs (must also be listed in the icon font's `glyphs:` in ui.yaml).
static const char *const ICON_UP = "\U000F05E0";       // check-circle
static const char *const ICON_DOWN = "\U000F0159";     // close-circle
static const char *const ICON_UNKNOWN = "\U000F0625";  // help-circle-outline
static const char *const ICON_HA = "\U000F07D0";       // home-assistant
static const char *const ICON_HA_OFF = "\U000F0026";   // alert
static const char *const ICON_WIFI_4 = "\U000F0928";   // wifi-strength-4
static const char *const ICON_WIFI_3 = "\U000F0925";   // wifi-strength-3
static const char *const ICON_WIFI_2 = "\U000F0922";   // wifi-strength-2
static const char *const ICON_WIFI_1 = "\U000F091F";   // wifi-strength-1
static const char *const ICON_WIFI_OFF = "\U000F092E"; // wifi-strength-off-outline

enum class Svc { UP, DOWN, UNKNOWN };

inline void set_text(lv_obj_t *label, const char *text) {
  const char *cur = lv_label_get_text(label);
  if (cur == nullptr || std::strcmp(cur, text) != 0)
    lv_label_set_text(label, text);
}

inline void set_hidden(lv_obj_t *obj, bool hidden) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != hidden) {
    if (hidden)
      lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

// Black-on-white (normal) or white-on-black (inverted) for a box and its text.
inline void set_inverted(lv_obj_t *box, bool inverted) {
  lv_color_t bg = inverted ? lv_color_black() : lv_color_white();
  lv_color_t fg = inverted ? lv_color_white() : lv_color_black();
  if (!lv_color_eq(lv_obj_get_style_bg_color(box, LV_PART_MAIN), bg)) {
    lv_obj_set_style_bg_color(box, bg, LV_PART_MAIN);
    lv_obj_set_style_text_color(box, fg, LV_PART_MAIN);
  }
}

// HA state string -> service state. `up_when` is the state that means healthy:
// "on" for connectivity/running sensors, "off" for problem-class sensors.
// Anything else (unavailable, unknown, empty, no data yet) is UNKNOWN, so a
// dead health check never reads as UP.
inline Svc parse_service(bool has_state, const std::string &state, const char *up_when) {
  if (!has_state)
    return Svc::UNKNOWN;
  const char *down_when = std::strcmp(up_when, "on") == 0 ? "off" : "on";
  if (state == up_when)
    return Svc::UP;
  if (state == down_when)
    return Svc::DOWN;
  return Svc::UNKNOWN;
}

// One service tile: outlined when UP, inverted when DOWN, thin border + "?"
// when UNKNOWN. Tiles with an empty name are hidden. The title label holds
// "<icon> <name>" (the font has the MDI glyphs merged in).
inline void set_service(lv_obj_t *tile, lv_obj_t *title_lbl, lv_obj_t *status_lbl, const char *name, Svc st) {
  bool unused = name == nullptr || name[0] == '\0';
  set_hidden(tile, unused);
  if (unused)
    return;
  set_inverted(tile, st == Svc::DOWN);
  int border = st == Svc::UNKNOWN ? 1 : 3;
  if (lv_obj_get_style_border_width(tile, LV_PART_MAIN) != border)
    lv_obj_set_style_border_width(tile, border, LV_PART_MAIN);
  const char *icon = ICON_UNKNOWN;
  const char *status = "NO DATA";
  switch (st) {
    case Svc::UP:
      icon = ICON_UP;
      status = "UP";
      break;
    case Svc::DOWN:
      icon = ICON_DOWN;
      status = "DOWN";
      break;
    case Svc::UNKNOWN:
      break;
  }
  std::string title = std::string(icon) + " " + name;
  set_text(title_lbl, title.c_str());
  set_text(status_lbl, status);
}

// One metric column. NaN renders as "--" with an empty bar, never as 0.
// At or above `alert_pct` the caption chip inverts.
inline void set_metric(lv_obj_t *value_lbl, lv_obj_t *bar, lv_obj_t *caption, lv_obj_t *note_lbl,
                       float value, float alert_pct) {
  char buf[8];
  bool valid = !std::isnan(value);
  if (valid) {
    int v = (int) std::lround(value);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    std::snprintf(buf, sizeof(buf), "%d%%", v);
    set_text(value_lbl, buf);
    if (lv_bar_get_value(bar) != v)
      lv_bar_set_value(bar, v, LV_ANIM_OFF);
    set_text(note_lbl, "");
  } else {
    set_text(value_lbl, "--");
    if (lv_bar_get_value(bar) != 0)
      lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    set_text(note_lbl, "no data");
  }
  set_inverted(caption, valid && value >= alert_pct);
}

inline const char *wifi_icon(bool connected, float rssi) {
  if (!connected || std::isnan(rssi))
    return ICON_WIFI_OFF;
  if (rssi >= -55) return ICON_WIFI_4;
  if (rssi >= -67) return ICON_WIFI_3;
  if (rssi >= -78) return ICON_WIFI_2;
  return ICON_WIFI_1;
}

// Text state for an address. Unknown/unavailable/empty shows "--".
inline std::string addr_or_dash(bool has_state, const std::string &s) {
  if (!has_state || s.empty() || s == "unknown" || s == "unavailable")
    return "--";
  return s;
}

}  // namespace dash
