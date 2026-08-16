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

// Authoritative figures from the host's statusline, when it is running.
int s_pct = -1;
time_t s_host_reset = 0;
uint32_t s_host_seen_ms = 0;
bool s_host_ever = false;

// Anything before this is the epoch the RTC boots to, not a real time.
const time_t kPlausibleEpoch = 1700000000;  // late 2023

uint32_t window_len_s() {
  return (uint32_t)config::v::window_hours() * 3600UL;
}

void persist() {
  s_prefs.putULong("start", (uint32_t)s_start);
  s_prefs.putUShort("prompts", s_prompts);
}

// Host figures survive a reboot, but only until the window they describe has
// passed — a stale percentage is worse than admitting we do not know.
bool host_fresh() {
  return s_pct >= 0 && s_host_reset > 0 && clock_valid() &&
         time(nullptr) < s_host_reset;
}

}  // namespace

void begin() {
  s_prefs.begin("usagewin", false);
  s_start = (time_t)s_prefs.getULong("start", 0);
  s_prompts = s_prefs.getUShort("prompts", 0);
  s_pct = s_prefs.getInt("pct", -1);
  s_host_reset = (time_t)s_prefs.getULong("hreset", 0);
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

void note_limits(int pct, time_t resets_at) {
  if (pct < 0 || pct > 100) return;
  s_pct = pct;
  s_host_reset = resets_at;
  s_host_seen_ms = millis();
  s_host_ever = true;
  s_prefs.putInt("pct", s_pct);
  s_prefs.putULong("hreset", (uint32_t)s_host_reset);
}

int percent() { return host_fresh() ? s_pct : -1; }

int32_t host_age_s() {
  if (!s_host_ever) return -1;
  return (int32_t)((millis() - s_host_seen_ms) / 1000);
}
bool host_data() { return host_fresh(); }

time_t window_start() { return s_start; }

time_t window_reset_at() {
  if (host_fresh()) return s_host_reset;
  return s_start == 0 ? 0 : s_start + (time_t)window_len_s();
}

uint32_t remaining_s() {
  const time_t at = window_reset_at();
  if (at == 0 || !clock_valid()) return 0;
  const time_t now = time(nullptr);
  return now >= at ? 0 : (uint32_t)(at - now);
}

uint32_t elapsed_s() {
  const uint32_t len = window_len_s();
  const uint32_t rem = remaining_s();
  return rem > len ? 0 : len - rem;
}

// Consumption when the host tells us, elapsed time when it does not. The two
// are different quantities and the screen labels which one it is showing.
float fraction() {
  if (host_fresh()) return (float)s_pct / 100.0f;
  if (s_start == 0) return 0.0f;
  return (float)elapsed_s() / (float)window_len_s();
}

uint16_t prompts() { return s_prompts; }

}  // namespace usage
