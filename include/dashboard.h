// Render helpers for the homelab dashboard.
//
// Everything is pure black/white: state is shown by inversion, outlines and
// hatching, never grey. Setters skip no-op changes, because every LVGL
// invalidation turns into an e-paper refresh.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "lvgl.h"

namespace dash {

// ---------------------------------------------------------------- basics

inline void set_text(lv_obj_t *label, const char *text) {
  const char *cur = lv_label_get_text(label);
  if (cur == nullptr || std::strcmp(cur, text) != 0)
    lv_label_set_text(label, text);
}
inline void set_text(lv_obj_t *label, const std::string &text) { set_text(label, text.c_str()); }

inline void set_hidden(lv_obj_t *obj, bool hidden) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden)
    return;
  if (hidden)
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

inline void set_bg(lv_obj_t *obj, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_bg_color(obj, LV_PART_MAIN), c))
    lv_obj_set_style_bg_color(obj, c, LV_PART_MAIN);
}
inline void set_fg(lv_obj_t *obj, lv_color_t c) {
  if (!lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), c))
    lv_obj_set_style_text_color(obj, c, LV_PART_MAIN);
}
inline void set_border(lv_obj_t *obj, int width, lv_color_t c) {
  if (lv_obj_get_style_border_width(obj, LV_PART_MAIN) != width)
    lv_obj_set_style_border_width(obj, width, LV_PART_MAIN);
  if (!lv_color_eq(lv_obj_get_style_border_color(obj, LV_PART_MAIN), c))
    lv_obj_set_style_border_color(obj, c, LV_PART_MAIN);
}

// Box (or label with bg_opa COVER) as black-on-white, or inverted.
inline void set_inverted(lv_obj_t *obj, bool inverted) {
  set_bg(obj, inverted ? lv_color_black() : lv_color_white());
  set_fg(obj, inverted ? lv_color_white() : lv_color_black());
}

// Round "LED" or signal bar: filled with `ink`, or an outline `ring` px wide
// on `paper` (the background it sits on).
inline void set_led(lv_obj_t *led, bool filled, int ring, lv_color_t ink, lv_color_t paper) {
  set_bg(led, filled ? ink : paper);
  set_border(led, filled ? 0 : ring, ink);
}

// ---------------------------------------------------------------- time

// Epoch seconds for an ISO-8601 timestamp as HA sends it
// ("2026-10-27T03:27:35+00:00"). Returns -1 if it doesn't parse.
inline int64_t parse_iso8601(const std::string &s) {
  int y, mo, d, h, mi, sec;
  if (s.size() < 19 || std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6)
    return -1;
  // days_from_civil (Howard Hinnant), proleptic Gregorian calendar.
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned) (y - era * 400);
  const unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t t = ((int64_t) era * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60 + sec;
  // Optional offset after the seconds: "+HH:MM", "-HH:MM" or "Z".
  const char *tz = std::strpbrk(s.c_str() + 19, "+-Z");
  int oh = 0, om = 0;
  if (tz != nullptr && *tz != 'Z' && std::sscanf(tz + 1, "%d:%d", &oh, &om) == 2)
    t -= (*tz == '+' ? 1 : -1) * (oh * 3600 + om * 60);
  return t;
}

// "45m", "6h", "12d": the largest whole unit.
inline std::string dur_short(int64_t secs) {
  if (secs < 0) secs = 0;
  char buf[16];
  if (secs < 3600)
    std::snprintf(buf, sizeof(buf), "%dm", (int) (secs / 60));
  else if (secs < 86400)
    std::snprintf(buf, sizeof(buf), "%dh", (int) (secs / 3600));
  else
    std::snprintf(buf, sizeof(buf), "%dd", (int) (secs / 86400));
  return buf;
}

// "15d 6h 20m".
inline std::string dur_long(int64_t secs) {
  if (secs < 0) secs = 0;
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%dd %dh %dm", (int) (secs / 86400), (int) (secs % 86400 / 3600),
                (int) (secs % 3600 / 60));
  return buf;
}

// ---------------------------------------------------------------- charts

constexpr int HIST_N = 31;  // one sample per minute: -30m .. now

struct History {
  float v[HIST_N];
  float drawn[HIST_N];  // what the canvas currently shows
  bool ever_drawn = false;
  History() {
    for (int i = 0; i < HIST_N; i++) v[i] = drawn[i] = NAN;
  }
  // Minute tick: drop the oldest sample. The "now" slot keeps the last value
  // until set_now overwrites it with the live one.
  void shift() {
    for (int i = 0; i < HIST_N - 1; i++) v[i] = v[i + 1];
  }
  void set_now(float value) { v[HIST_N - 1] = value; }
  bool changed() const {
    if (!ever_drawn) return true;
    for (int i = 0; i < HIST_N; i++) {
      bool a = std::isnan(v[i]), b = std::isnan(drawn[i]);
      if (a != b || (!a && std::lround(v[i]) != std::lround(drawn[i]))) return true;
    }
    return false;
  }
};
inline History hist[3];  // cpu, mem, gpu

// Hatched area chart: 2px line, 2px baseline, diagonal hatch under the line.
// Fixed 0-100 scale so the three charts compare; NaN samples leave gaps.
// Writes the RGB565 buffer directly (black/white are 0x0000/0xFFFF, so byte
// order doesn't matter) and invalidates once.
inline void draw_chart(lv_obj_t *canvas, History &h) {
  if (!h.changed()) return;
  lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);
  if (buf == nullptr || buf->header.cf != LV_COLOR_FORMAT_RGB565) return;
  const int w = buf->header.w, ht = buf->header.h, stride = buf->header.stride;
  uint8_t *data = buf->data;
  auto row = [&](int y) { return reinterpret_cast<uint16_t *>(data + y * stride); };
  auto px = [&](int x, int y) {
    if (x >= 0 && x < w && y >= 0 && y < ht) row(y)[x] = 0x0000;
  };
  for (int y = 0; y < ht; y++)
    for (int x = 0; x < w; x++) row(y)[x] = 0xFFFF;

  const int base = ht - 2;  // top row of the 2px baseline
  auto ymap = [&](float v) {
    v = std::max(0.0f, std::min(100.0f, v));
    return (int) std::lround((ht - 2) - v * (ht - 4) / 100.0f);  // 2px inset
  };
  int prev_y = -1;
  for (int x = 0; x < w; x++) {
    float pos = x * (HIST_N - 1) / (float) (w - 1);
    int i = std::min((int) pos, HIST_N - 2);
    float a = h.v[i], b = h.v[i + 1];
    if (std::isnan(a) || std::isnan(b)) {
      prev_y = -1;
      continue;
    }
    int y = ymap(a + (b - a) * (pos - i));
    for (int yy = y + 2; yy < base; yy++)
      if ((x + yy) % 4 == 0) px(x, yy);  // diagonal hatch
    int y0 = prev_y < 0 ? y : std::min(prev_y, y);
    int y1 = prev_y < 0 ? y : std::max(prev_y, y);
    for (int yy = y0; yy <= y1 + 1; yy++) px(x, yy);  // 2px line, joined vertically
    prev_y = y;
  }
  for (int y = base; y < ht; y++)
    for (int x = 0; x < w; x++) px(x, y);

  lv_obj_invalidate(canvas);
  std::copy(h.v, h.v + HIST_N, h.drawn);
  h.ever_drawn = true;
}

// One metric column: big value, caption (inverts at >= alert), chart, and a
// centred note when there is nothing to plot ("NOT WIRED" when no entity is
// configured, else "NO DATA").
inline void set_metric(lv_obj_t *value_lbl, lv_obj_t *caption, lv_obj_t *canvas, lv_obj_t *note,
                       History &h, float value, bool wired, float alert_pct) {
  const bool valid = wired && !std::isnan(value);
  h.set_now(valid ? value : NAN);
  if (valid) {
    char buf[8];
    int v = std::max(0, std::min(100, (int) std::lround(value)));
    std::snprintf(buf, sizeof(buf), "%d%%", v);
    set_text(value_lbl, buf);
  } else {
    set_text(value_lbl, "—");  // em dash
  }
  bool any = false;
  for (float s : h.v) any |= !std::isnan(s);
  set_hidden(note, any);
  if (!any) set_text(note, wired ? "NO DATA" : "NOT WIRED");
  set_inverted(caption, valid && value >= alert_pct);
  draw_chart(canvas, h);
}

// ---------------------------------------------------------------- services

enum class Svc { UP, DOWN, UNKNOWN };

// HA state -> service state. `up_when` is the healthy state: "on" for
// running/connectivity sensors, "off" for problem-class ones. Anything else,
// including unavailable or no data yet, is UNKNOWN, so a dead check never
// reads as UP.
inline Svc parse_service(bool has_state, const std::string &state, const char *up_when) {
  if (!has_state) return Svc::UNKNOWN;
  const char *down_when = std::strcmp(up_when, "on") == 0 ? "off" : "on";
  if (state == up_when) return Svc::UP;
  if (state == down_when) return Svc::DOWN;
  return Svc::UNKNOWN;
}

// When each service went DOWN, as seen by this device. 0 = not known, e.g.
// it was already down at boot, so we show "DOWN" without a duration.
inline int64_t down_since[6] = {0, 0, 0, 0, 0, 0};
inline Svc last_state[6] = {Svc::UNKNOWN, Svc::UNKNOWN, Svc::UNKNOWN,
                            Svc::UNKNOWN, Svc::UNKNOWN, Svc::UNKNOWN};

// One service cell.
//   UP:      filled LED, "up 12d" (from the HA uptime entity, a boot timestamp)
//   DOWN:    whole cell inverted, hollow 2px LED, "DOWN 4m"
//   UNKNOWN: hollow 1px LED, "no data"
// An empty name hides the cell.
inline void set_service(int idx, lv_obj_t *cell, lv_obj_t *led, lv_obj_t *name_lbl, lv_obj_t *line2,
                        const char *name, Svc st, bool uptime_ok, const std::string &uptime_iso,
                        int64_t now) {
  const bool unused = name == nullptr || name[0] == '\0';
  set_hidden(cell, unused);
  if (unused) return;
  set_text(name_lbl, name);

  if (st == Svc::DOWN && last_state[idx] != Svc::DOWN)
    down_since[idx] = (last_state[idx] == Svc::UP && now > 0) ? now : 0;
  if (st != Svc::DOWN) down_since[idx] = 0;
  last_state[idx] = st;

  const bool down = st == Svc::DOWN;
  set_inverted(cell, down);
  set_led(led, st == Svc::UP, down ? 2 : 1, down ? lv_color_white() : lv_color_black(),
          down ? lv_color_black() : lv_color_white());

  std::string text;
  switch (st) {
    case Svc::UP: {
      int64_t boot = uptime_ok ? parse_iso8601(uptime_iso) : -1;
      text = (boot > 0 && now > 0) ? "up " + dur_short(now - boot) : std::string("up");
      break;
    }
    case Svc::DOWN:
      text = down_since[idx] > 0 ? "DOWN " + dur_short(now - down_since[idx]) : std::string("DOWN");
      break;
    case Svc::UNKNOWN:
      text = "no data";
      break;
  }
  set_text(line2, text);
}

// ---------------------------------------------------------------- header / footer

// Number of filled signal bars (0-4).
inline int wifi_level(bool connected, float rssi) {
  if (!connected || std::isnan(rssi)) return 0;
  if (rssi > -55) return 4;
  if (rssi > -67) return 3;
  if (rssi > -75) return 2;
  return 1;
}

// Address text, or "--" when unknown/unavailable/empty.
inline std::string addr_or_dash(bool has_state, const std::string &s) {
  if (!has_state || s.empty() || s == "unknown" || s == "unavailable") return "--";
  return s;
}

}  // namespace dash
