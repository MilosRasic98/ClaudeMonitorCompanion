#pragma once
#include <stdint.h>
#include <time.h>

// The rolling usage window.
//
// Anthropic's limit window opens with your first message and runs for a fixed
// number of hours. That much is derivable from the hook events the board
// already receives, so the board can say how far through the window you are and
// when it resets.
//
// Real consumption comes from the host. Claude Code's *statusline* JSON carries
// `rate_limits.five_hour.used_percentage` and `.resets_at` — hooks do not — so a
// statusline script forwards those to the board and they are authoritative when
// present.
//
// Without them the board falls back to timing the window from the prompts it has
// seen. That fallback is only ever a lower bound: it cannot know about work done
// before the board was switched on, so it reads the window as opening later than
// it really did.

namespace usage {

void begin();

// Wall-clock time via NTP, needed to display a reset time rather than a
// countdown. Safe to call repeatedly; only acts once the network is up.
void tick(bool network_up);
bool clock_valid();

// Call when the host reports a user prompt.
void note_prompt();

// Real figures from the statusline. pct is 0..100; resets_at is epoch seconds.
void note_limits(int pct, time_t resets_at);

// -1 when the host has never reported. Callers must show something honest
// rather than inventing a number.
int percent();
bool host_data();

time_t window_start();      // 0 if no window is open
time_t window_reset_at();   // host value when known, else derived; 0 if neither
uint32_t elapsed_s();
uint32_t remaining_s();
float fraction();           // 0..1 through the window
uint16_t prompts();         // prompts seen inside the current window

}  // namespace usage
