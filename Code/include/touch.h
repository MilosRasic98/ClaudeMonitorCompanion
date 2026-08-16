#pragma once
#include <stdint.h>

namespace touch {

enum class Gesture : uint8_t {
  None = 0, SwipeUp, SwipeDown, SwipeLeft, SwipeRight,
  Tap, DoubleTap, LongPress,
};

struct Event {
  bool     pressed;   // finger currently down
  int16_t  x, y;      // panel coords, 0..239 / 0..279, valid when pressed
  Gesture  gesture;   // gesture reported for this event, if any
};

// Hardware reset + probe. Returns false if the chip does not answer.
// Wire must already be initialised. Safe to call once from setup().
bool begin();

// Non-blocking. Returns true and fills `out` when there is a new event
// (press, release, move, or gesture) since the last call. Returns false
// when nothing changed. Intended to be called every frame.
bool poll(Event &out);

// Chip firmware version / chip id, for logging. 0 if unread.
uint8_t chip_id();

}  // namespace touch
