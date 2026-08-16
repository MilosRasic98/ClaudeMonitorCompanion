#pragma once
#include <stdint.h>

// A 3x5 pixel font, scaled by whole numbers only.
//
// Nothing on this board could draw text until now — the face is rectangles. A
// 3x5 bitmap font is the smallest that stays legible, and scaling it by integers
// keeps every glyph edge on the grid, so text looks like it belongs next to the
// mascot rather than pasted on from a different project.
//
// Uppercase only. Lowercase input is folded up; unknown characters draw blank
// rather than a box, because a missing glyph should be quiet.

namespace text {

const int kGlyphW = 3;
const int kGlyphH = 5;

// Width in pixels of `s` rendered at `scale`, including inter-glyph gaps.
int width(const char *s, int scale);
int height(int scale);

void draw(int x, int y, const char *s, int scale, uint16_t color);

// Same, horizontally centred on the panel.
void draw_centered(int y, const char *s, int scale, uint16_t color);

}  // namespace text
