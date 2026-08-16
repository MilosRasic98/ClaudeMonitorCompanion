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
// What it deliberately does NOT claim to know is how much of the quota you have
// actually consumed. No Claude Code hook reports that — the only limit-related
// signal is StopFailure/rate_limit, which arrives once you have already hit it.
// So this measures time, and says so on screen.
//
// Caveat worth remembering: the board only sees prompts while it is powered and
// connected. Work done with the board off does not move the window, so after an
// outage the reset time can read early.

namespace usage {

void begin();

// Wall-clock time via NTP, needed to display a reset time rather than a
// countdown. Safe to call repeatedly; only acts once the network is up.
void tick(bool network_up);
bool clock_valid();

// Call when the host reports a user prompt.
void note_prompt();

time_t window_start();      // 0 if no window is open
time_t window_reset_at();   // 0 if no window is open
uint32_t elapsed_s();
uint32_t remaining_s();
float fraction();           // 0..1 through the window
uint16_t prompts();         // prompts seen inside the current window

}  // namespace usage
