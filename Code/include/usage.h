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
void note_limits(int pct, time_t resets_at, int wpct, time_t wresets_at);

// -1 when the host has never reported. Callers must show something honest
// rather than inventing a number.
int percent();
bool host_data();

// Seconds since the host last reported, or -1 if it never has. The statusline
// should refresh at least once a minute, so a large value means it is not
// running rather than that nothing has changed.
int32_t host_age_s();

// The host can report on a timer while its figures are frozen: a Claude Code
// session only refreshes its rate-limit snapshot when it makes an API call, so
// an idle session forwards the same number forever while the work happens
// somewhere else. The board can spot it, because the hooks keep telling it
// prompts are being submitted. Many prompts and no change means the percentage
// is stale, however punctually it arrives.
bool percent_suspect();

// The seven-day, all-models bucket. Same source, same caveats.
int weekly_percent();
time_t weekly_reset_at();
uint32_t weekly_remaining_s();


time_t window_start();      // 0 if no window is open
time_t window_reset_at();   // host value when known, else derived; 0 if neither
uint32_t elapsed_s();
uint32_t remaining_s();
float fraction();           // 0..1 through the window
uint16_t prompts();         // prompts seen inside the current window

}  // namespace usage
