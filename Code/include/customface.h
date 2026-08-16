#pragma once
#include <stddef.h>
#include <stdint.h>

// A face the user draws themselves, on the settings page.
//
// 24 x 28 cells at 10 screen pixels each is exactly 240 x 280, so the grid maps
// one-to-one onto the panel with no scaling and no rounding.
//
// Two frames per face: resting and blink. A static custom face would sit dead
// next to the built-in ones, which all blink — and a blink is the cheapest
// possible animation, one extra frame. If the blink frame is empty the board
// simply does not blink.
//
// Four slots, so a few faces can be drawn and swiped between. Only slots that
// actually hold a drawing join the rotation; empty ones would be dead screens.

namespace customface {

const int kW = 24;
const int kH = 28;
const int kCell = 10;
const int kCells = kW * kH;              // 672
const int kFrameBytes = kCells / 4;      // 2 bits per cell -> 168
const int kSlotBytes = kFrameBytes * 2;  // 336 per face
const int kSlots = 4;

enum Ink : uint8_t { kBg = 0, kInk = 1, kLit = 2 };

void begin();

// Raw bytes for one slot: frame 0 then frame 1, row-major, four cells per byte,
// most significant bits first. Returns false on a bad slot or wrong length.
bool store(int slot, const uint8_t *data, size_t len);
const uint8_t *data(int slot);

bool has_drawing(int slot);  // frame 0 has any non-background cell
bool has_blink(int slot);    // frame 1 does too, so blinking is worth doing
bool any_drawing();
int used_count();

// Paint a frame. `full` forces every cell; otherwise only cells that differ
// from the other frame are drawn, which is what makes a blink cheap.
void draw(int slot, int frame, uint16_t bg, bool full);

}  // namespace customface
