#pragma once
#include <stdint.h>

// Vertical swipes move between screens; horizontal swipes still change the
// face's style. The mascot is the resting screen and the one the shell is built
// around — the others are things you go and look at, then leave.

namespace views {

enum class View : uint8_t {
  Face,    // the mascot
  Gauge,   // the five-hour limit, with time to reset
  Weekly,  // the seven-day, all-models limit
  Stats,   // plain diagnostics
  Count,
};

void begin();

// Safe from any task. Applied at the top of the next frame, because esp_lcd is
// not thread-safe and drawing must stay on the render task.
void cycle(int delta);

View current();
const char *name();

// Draws the current screen. Returns false for View::Face, which the caller
// renders instead — the face has its own dirty-tracking and would be wasteful
// to route through here.
bool render(uint32_t now_ms, uint16_t bg);

}  // namespace views
