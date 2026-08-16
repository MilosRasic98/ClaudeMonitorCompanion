#include "views.h"

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "mood.h"
#include "pins.h"
#include "text.h"
#include "tuning.h"
#include "usage.h"

namespace views {
namespace {

View s_view = View::Face;
volatile int8_t s_pending = 0;
bool s_entered = false;      // has the current view painted its background yet
uint32_t s_next_update = 0;  // these screens change once a second at most
uint16_t s_bg = 0;

// Erase a full-width band before drawing into it. Wasteful in pixels and
// trivial in practice: a band is a couple of milliseconds and these screens
// redraw once a second, not sixty times.
void band(int y, int h) { display::fill_rect(0, y, LCD_W, h, s_bg); }

// Erase only a centred column. The percentage sits inside the ring, and a
// full-width erase would cut a slot straight through it.
void band_centered(int y, int h, int w) {
  display::fill_rect((LCD_W - w) / 2, y, w, h, s_bg);
}

void centered(int y, const char *s, int scale) {
  text::draw_centered(y, s, scale, kBlack);
}

// ------------------------------------------------------------------ dials --

// Three screens share one renderer: a title, a ring, a big number inside it and
// an optional line underneath. They differ only in what they measure.
const int kRingCx = LCD_W / 2;
const int kRingCy = 134;
const int kRingRo = 86;   // outer radius
const int kRingRi = 66;   // inner radius

int s_last_pct_drawn = -999;
char s_last_big[10] = "";
char s_last_sub[18] = "";

// The unfilled part of the ring is the background lightened toward mid-tone, so
// the track reads on both the orange field and the red alert field without a
// hardcoded colour. Too dark and it is indistinguishable from the filled arc,
// which is the one distinction a dial exists to make.
uint16_t darken(uint16_t swapped) {
  const uint16_t c = (uint16_t)((swapped >> 8) | (swapped << 8));  // undo the byte swap
  uint16_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = (uint16_t)(r * 70 / 100);
  g = (uint16_t)(g * 70 / 100);
  b = (uint16_t)(b * 70 / 100);
  const uint16_t out = (uint16_t)((r << 11) | (g << 5) | b);
  return (uint16_t)((out >> 8) | (out << 8));
}

void draw_arc(float frac) {
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  const float sweep = frac * 2.0f * (float)PI;
  const uint16_t track = darken(s_bg);
  const int ro2 = kRingRo * kRingRo, ri2 = kRingRi * kRingRi;

  for (int y = kRingCy - kRingRo; y <= kRingCy + kRingRo; y++) {
    if (y < 0 || y >= LCD_H) continue;
    const int dy = y - kRingCy;
    int run_x = -1;
    uint16_t run_c = 0;

    // One past the right edge, so a run touching the edge still gets flushed.
    for (int x = kRingCx - kRingRo; x <= kRingCx + kRingRo + 1; x++) {
      bool on = false;
      uint16_t c = 0;
      if (x <= kRingCx + kRingRo) {
        const int dx = x - kRingCx;
        const int d2 = dx * dx + dy * dy;
        if (d2 <= ro2 && d2 >= ri2) {
          float a = atan2f((float)dx, (float)-dy);  // clockwise from twelve
          if (a < 0.0f) a += 2.0f * (float)PI;
          c = (a <= sweep) ? kBlack : track;
          on = true;
        }
      }
      if (run_x >= 0 && (!on || c != run_c)) {
        display::fill_rect(run_x, y, x - run_x, 1, run_c);
        run_x = -1;
      }
      if (on && run_x < 0) {
        run_x = x;
        run_c = c;
      }
    }
  }
}

// Only repaint text that actually changed. The percentage moves every few
// minutes and the countdown once a minute; repainting either at 1 Hz would
// flicker for no reason. clear_w of 0 erases the full width, anything else a
// centred column — text inside the ring must not cut a slot through it.
void text_slot(char *cache, size_t n, int y, int h, const char *s, int scale,
               int clear_w) {
  if (strncmp(cache, s, n) == 0) return;
  strncpy(cache, s, n - 1);
  cache[n - 1] = '\0';
  if (clear_w > 0) band_centered(y, h, clear_w);
  else band(y, h);
  centered(y, s, scale);
}

void dial_static(const char *title) {
  display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
  centered(14, title, 4);
  s_last_pct_drawn = -999;
  s_last_big[0] = '\0';
  s_last_sub[0] = '\0';
}

// `sub` may be empty, which is how the weekly screen drops the line underneath.
void dial_dynamic(int pct_for_arc, const char *big, const char *sub) {
  if (pct_for_arc != s_last_pct_drawn) {
    s_last_pct_drawn = pct_for_arc;
    draw_arc(pct_for_arc / 100.0f);
    s_last_big[0] = '\0';
  }
  text_slot(s_last_big, sizeof(s_last_big), kRingCy - 22, 46, big, 8, 112);
  text_slot(s_last_sub, sizeof(s_last_sub), 236, 22, sub, 4, 0);
}

void gauge_dynamic() {
  char big[10], sub[18];
  int pct;

  if (!usage::clock_valid()) {
    snprintf(big, sizeof(big), "--");
    snprintf(sub, sizeof(sub), "NO CLOCK");
    pct = 0;
  } else if (usage::host_data()) {
    pct = usage::percent();
    snprintf(big, sizeof(big), "%d%%", pct);
    const uint32_t r = usage::remaining_s();
    snprintf(sub, sizeof(sub), "%u:%02u LEFT", (unsigned)(r / 3600),
             (unsigned)((r % 3600) / 60));
  } else if (usage::window_start() != 0) {
    // No statusline reporting. Show elapsed time, and say that is what it is,
    // rather than dressing a clock up as a quota.
    pct = (int)(usage::fraction() * 100.0f);
    const uint32_t r = usage::remaining_s();
    snprintf(big, sizeof(big), "%u:%02u", (unsigned)(r / 3600),
             (unsigned)((r % 3600) / 60));
    snprintf(sub, sizeof(sub), "EST NO HOST");
  } else {
    pct = 0;
    snprintf(big, sizeof(big), "IDLE");
    snprintf(sub, sizeof(sub), "NO DATA YET");
  }
  dial_dynamic(pct, big, sub);
}

void weekly_dynamic() {
  char big[10];
  const int pct = usage::weekly_percent();
  if (pct < 0) {
    snprintf(big, sizeof(big), "--");
    dial_dynamic(0, big, "");
    return;
  }
  snprintf(big, sizeof(big), "%d%%", pct);
  dial_dynamic(pct, big, "");
}

// ------------------------------------------------------------------ stats --

void stats_static() {
  display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
  centered(18, "STATUS", 3);
}

void stats_row(int y, const char *label, const char *value) {
  band(y, text::height(2) + 2);
  text::draw(14, y, label, 2, kBlack);
  text::draw(LCD_W - 14 - text::width(value, 2), y, value, 2, kBlack);
}

void stats_dynamic() {
  char buf[24];

  stats_row(58, "IP", WiFi.status() == WL_CONNECTED
                          ? WiFi.localIP().toString().c_str()
                          : "OFFLINE");

  snprintf(buf, sizeof(buf), "%d DBM", (int)WiFi.RSSI());
  stats_row(84, "SIGNAL", buf);

  const uint32_t up = millis() / 1000;
  snprintf(buf, sizeof(buf), "%uH %02uM", (unsigned)(up / 3600),
           (unsigned)((up % 3600) / 60));
  stats_row(110, "UPTIME", buf);

  stats_row(136, "MOOD", mood::name(mood::current()));

  snprintf(buf, sizeof(buf), "%u", (unsigned)mood::energy());
  stats_row(162, "ENERGY", buf);

  snprintf(buf, sizeof(buf), "%u", (unsigned)usage::prompts());
  stats_row(188, "PROMPTS", buf);

  if (usage::clock_valid()) {
    const time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    snprintf(buf, sizeof(buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
  } else {
    snprintf(buf, sizeof(buf), "--:--");
  }
  stats_row(214, "TIME", buf);
}

}  // namespace

void begin() {
  s_view = View::Face;
  s_entered = false;
}

void cycle(int delta) { s_pending = (int8_t)(s_pending + delta); }

View current() { return s_view; }

const char *name() {
  switch (s_view) {
    case View::Gauge:  return "gauge";
    case View::Weekly: return "weekly";
    case View::Stats:  return "stats";
    default:          return "face";
  }
}

bool render(uint32_t now_ms, uint16_t bg) {
  const int8_t delta = s_pending;
  if (delta) {
    s_pending = (int8_t)(s_pending - delta);
    const int n = (int)View::Count;
    s_view = (View)((((int)s_view + delta) % n + n) % n);
    s_entered = false;
    Serial.printf("view  : %s\n", name());
  }

  if (s_view == View::Face) return false;

  const bool bg_changed = bg != s_bg;
  s_bg = bg;

  if (!s_entered || bg_changed) {
    s_entered = true;
    switch (s_view) {
      case View::Gauge:  dial_static("5H LIMIT"); break;
      case View::Weekly: dial_static("WEEKLY"); break;
      default:           stats_static(); break;
    }
    s_next_update = 0;
  }

  if (now_ms >= s_next_update) {
    s_next_update = now_ms + 1000;
    switch (s_view) {
      case View::Gauge:  gauge_dynamic(); break;
      case View::Weekly: weekly_dynamic(); break;
      default:           stats_dynamic(); break;
    }
  }
  return true;
}

}  // namespace views
