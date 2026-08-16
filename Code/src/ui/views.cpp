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

void centered(int y, const char *s, int scale) {
  text::draw_centered(y, s, scale, kBlack);
}

// ------------------------------------------------------------------ gauge --

// A ring of chunky blocks rather than a smooth arc. A real anti-aliased arc
// would fight the pixel-art face, and drawing one on this grid means testing
// every cell in a 200x200 box — 1600 SPI transactions. Twenty-four positioned
// blocks cost twenty-four, and read better.
const int kRingCx = LCD_W / 2;
const int kRingCy = 112;
const int kRingR = 84;
const int kRingSegs = 24;
const int kRingBlock = 14;

int s_last_filled = -1;
char s_last_big[8] = "";
char s_last_sub[16] = "";

void ring_block(int i, bool on) {
  const float a = ((float)i * 360.0f / kRingSegs - 90.0f) * (float)PI / 180.0f;
  const int q = config::v::grid_q();
  const int cx = kRingCx + (int)(kRingR * cosf(a));
  const int cy = kRingCy + (int)(kRingR * sinf(a));
  const int x = (cx - kRingBlock / 2) / q * q;
  const int y = (cy - kRingBlock / 2) / q * q;
  if (on) {
    display::fill_rect(x, y, kRingBlock, kRingBlock, kBlack);
  } else {
    // Unfilled positions stay as a stub so the ring reads as a dial rather
    // than a partial arc floating in space.
    display::fill_rect(x, y, kRingBlock, kRingBlock, s_bg);
    display::fill_rect(x + kRingBlock / 4, y + kRingBlock / 4, kRingBlock / 2,
                       kRingBlock / 2, kBlack);
  }
}

void draw_ring(float frac) {
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;
  const int filled = (int)(frac * kRingSegs + 0.5f);
  if (filled == s_last_filled) return;
  s_last_filled = filled;
  for (int i = 0; i < kRingSegs; i++) ring_block(i, i < filled);
}

// Only repaint text that actually changed. The percentage moves every few
// minutes and the countdown once a minute; repainting either at 1 Hz would
// flicker for no reason.
void text_slot(char *cache, size_t n, int y, int h, const char *s, int scale) {
  if (strncmp(cache, s, n) == 0) return;
  strncpy(cache, s, n - 1);
  cache[n - 1] = '\0';
  band(y, h);
  centered(y, s, scale);
}

void gauge_static() {
  display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
  s_last_filled = -1;
  s_last_big[0] = '\0';
  s_last_sub[0] = '\0';
  centered(8, "5H LIMIT", 2);
}

void gauge_dynamic() {
  char big[8], sub[16];

  if (!usage::clock_valid()) {
    snprintf(big, sizeof(big), "--");
    snprintf(sub, sizeof(sub), "NO CLOCK");
    draw_ring(0.0f);
  } else if (usage::host_data()) {
    snprintf(big, sizeof(big), "%d%%", usage::percent());
    const uint32_t r = usage::remaining_s();
    snprintf(sub, sizeof(sub), "%u:%02u TO RESET", (unsigned)(r / 3600),
             (unsigned)((r % 3600) / 60));
    draw_ring(usage::fraction());
  } else if (usage::window_start() != 0) {
    // No statusline reporting. Show elapsed time, and say that is what it is,
    // rather than dressing a clock up as a quota.
    const uint32_t r = usage::remaining_s();
    snprintf(big, sizeof(big), "%u:%02u", (unsigned)(r / 3600),
             (unsigned)((r % 3600) / 60));
    snprintf(sub, sizeof(sub), "EST - NO HOST");
    draw_ring(usage::fraction());
  } else {
    snprintf(big, sizeof(big), "IDLE");
    snprintf(sub, sizeof(sub), "NO DATA YET");
    draw_ring(0.0f);
  }

  text_slot(s_last_big, sizeof(s_last_big), 92, 44, big, 8);
  text_slot(s_last_sub, sizeof(s_last_sub), 232, 14, sub, 2);
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
