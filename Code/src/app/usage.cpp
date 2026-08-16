#include "usage.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"

namespace usage {
namespace {

Preferences s_prefs;

time_t s_start = 0;      // epoch of the first prompt in this window
uint16_t s_prompts = 0;
bool s_ntp_started = false;

// Anything before this is the epoch the RTC boots to, not a real time.
const time_t kPlausibleEpoch = 1700000000;  // late 2023

uint32_t window_len_s() {
  return (uint32_t)config::v::window_hours() * 3600UL;
}

void persist() {
  s_prefs.putULong("start", (uint32_t)s_start);
  s_prefs.putUShort("prompts", s_prompts);
}

}  // namespace

void begin() {
  s_prefs.begin("usagewin", false);
  s_start = (time_t)s_prefs.getULong("start", 0);
  s_prompts = s_prefs.getUShort("prompts", 0);
}

void tick(bool network_up) {
  if (s_ntp_started || !network_up) return;
  // configTzTime applies the POSIX timezone rule as well as starting SNTP, so
  // localtime() is correct without any manual offset arithmetic — including
  // daylight saving, which is exactly the thing you do not want to hand-roll.
  configTzTime(config::get_str("tz"), "pool.ntp.org", "time.nist.gov");
  s_ntp_started = true;
}

bool clock_valid() { return time(nullptr) > kPlausibleEpoch; }

void note_prompt() {
  if (!clock_valid()) return;  // without a real clock the window is meaningless
  const time_t now = time(nullptr);

  if (s_start == 0 || now >= s_start + (time_t)window_len_s()) {
    s_start = now;
    s_prompts = 1;
  } else {
    s_prompts++;
  }
  persist();
}

time_t window_start() { return s_start; }

time_t window_reset_at() {
  return s_start == 0 ? 0 : s_start + (time_t)window_len_s();
}

uint32_t elapsed_s() {
  if (s_start == 0 || !clock_valid()) return 0;
  const time_t now = time(nullptr);
  if (now < s_start) return 0;
  const uint32_t e = (uint32_t)(now - s_start);
  return e > window_len_s() ? window_len_s() : e;
}

uint32_t remaining_s() {
  if (s_start == 0 || !clock_valid()) return 0;
  return window_len_s() - elapsed_s();
}

float fraction() {
  if (s_start == 0) return 0.0f;
  return (float)elapsed_s() / (float)window_len_s();
}

uint16_t prompts() { return s_prompts; }

}  // namespace usage
