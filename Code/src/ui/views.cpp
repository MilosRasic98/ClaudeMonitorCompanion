#include "views.h"

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
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

void centered(int y, const char *s, int scale) {
  text::draw_centered(y, s, scale, kBlack);
}

// ------------------------------------------------------------------ gauge --

const int kBarX = 20;
const int kBarY = 150;
const int kBarH = 26;
const int kSegs = 10;
const int kSegW = 15;
const int kSegGap = 5;

void draw_bar(float frac) {
  band(kBarY, kBarH);
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  const int filled = (int)(frac * kSegs + 0.5f);

  for (int i = 0; i < kSegs; i++) {
    const int x = kBarX + i * (kSegW + kSegGap);
    if (i < filled) {
      display::fill_rect(x, kBarY, kSegW, kBarH, kBlack);
    } else {
      // Empty segments stay visible as a stub, so the track reads as a scale
      // rather than the bar simply ending.
      const int q = config::v::grid_q();
      display::fill_rect(x, kBarY + kBarH - q, kSegW, q, kBlack);
    }
  }
}

void gauge_static() {
  display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
  char title[16];
  snprintf(title, sizeof(title), "%dH WINDOW", config::v::window_hours());
  centered(24, title, 3);
}

void gauge_dynamic() {
  if (!usage::clock_valid()) {
    band(58, 60);
    centered(70, "NO CLOCK", 5);
    draw_bar(0.0f);
    band(196, 60);
    centered(200, "WAITING FOR TIME", 2);
    return;
  }
  if (usage::window_start() == 0) {
    band(58, 60);
    centered(70, "IDLE", 6);
    draw_bar(0.0f);
    band(196, 60);
    centered(200, "NO WINDOW OPEN", 2);
    return;
  }

  const uint32_t rem = usage::remaining_s();
  char big[8];
  snprintf(big, sizeof(big), "%u:%02u", (unsigned)(rem / 3600),
           (unsigned)((rem % 3600) / 60));

  band(58, 62);
  centered(58, big, 9);
  band(122, 14);
  centered(122, "LEFT", 2);

  draw_bar(usage::fraction());

  const time_t at = usage::window_reset_at();
  struct tm tm_at;
  localtime_r(&at, &tm_at);
  char clock[8];
  snprintf(clock, sizeof(clock), "%02d:%02d", tm_at.tm_hour, tm_at.tm_min);

  band(192, 16);
  centered(192, "RESETS AT", 2);
  band(214, 32);
  centered(214, clock, 6);
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
    case View::Gauge: return "gauge";
    case View::Stats: return "stats";
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
  }

  if (s_view == View::Face) return false;

  const bool bg_changed = bg != s_bg;
  s_bg = bg;

  if (!s_entered || bg_changed) {
    s_entered = true;
    if (s_view == View::Gauge) gauge_static();
    else stats_static();
    s_next_update = 0;
  }

  if (now_ms >= s_next_update) {
    s_next_update = now_ms + 1000;
    if (s_view == View::Gauge) gauge_dynamic();
    else stats_dynamic();
  }
  return true;
}

}  // namespace views
