#include "text.h"

#include <Arduino.h>
#include <string.h>

#include "display.h"
#include "pins.h"

namespace text {
namespace {

// Each glyph is five rows of three bits, high bit leftmost. Written as binary
// so the shapes are readable in the source — a font table in hex is unreviewable.
struct Glyph {
  char c;
  uint8_t row[kGlyphH];
};

#define G(a, b, c, d, e) {0b##a, 0b##b, 0b##c, 0b##d, 0b##e}

const Glyph kFont[] = {
    {'0', G(111, 101, 101, 101, 111)},
    {'1', G(010, 110, 010, 010, 111)},
    {'2', G(111, 001, 111, 100, 111)},
    {'3', G(111, 001, 111, 001, 111)},
    {'4', G(101, 101, 111, 001, 001)},
    {'5', G(111, 100, 111, 001, 111)},
    {'6', G(111, 100, 111, 101, 111)},
    {'7', G(111, 001, 001, 001, 001)},
    {'8', G(111, 101, 111, 101, 111)},
    {'9', G(111, 101, 111, 001, 111)},

    {'A', G(111, 101, 111, 101, 101)},
    {'B', G(110, 101, 110, 101, 110)},
    {'C', G(111, 100, 100, 100, 111)},
    {'D', G(110, 101, 101, 101, 110)},
    {'E', G(111, 100, 111, 100, 111)},
    {'F', G(111, 100, 111, 100, 100)},
    {'G', G(111, 100, 101, 101, 111)},
    {'H', G(101, 101, 111, 101, 101)},
    {'I', G(111, 010, 010, 010, 111)},
    {'J', G(001, 001, 001, 101, 111)},
    {'K', G(101, 101, 110, 101, 101)},
    {'L', G(100, 100, 100, 100, 111)},
    {'M', G(101, 111, 111, 101, 101)},
    {'N', G(110, 101, 101, 101, 101)},
    {'O', G(111, 101, 101, 101, 111)},
    {'P', G(111, 101, 111, 100, 100)},
    {'Q', G(111, 101, 101, 111, 011)},
    {'R', G(111, 101, 110, 101, 101)},
    {'S', G(111, 100, 111, 001, 111)},
    {'T', G(111, 010, 010, 010, 010)},
    {'U', G(101, 101, 101, 101, 111)},
    {'V', G(101, 101, 101, 101, 010)},
    {'W', G(101, 101, 111, 111, 101)},
    {'X', G(101, 101, 010, 101, 101)},
    {'Y', G(101, 101, 010, 010, 010)},
    {'Z', G(111, 001, 010, 100, 111)},

    {':', G(000, 010, 000, 010, 000)},
    {'-', G(000, 000, 111, 000, 000)},
    {'.', G(000, 000, 000, 000, 010)},
    {'/', G(001, 001, 010, 100, 100)},
    {'%', G(101, 001, 010, 100, 101)},
    {'?', G(111, 001, 010, 000, 010)},
    {' ', G(000, 000, 000, 000, 000)},
};

const size_t kGlyphCount = sizeof(kFont) / sizeof(kFont[0]);

const Glyph *find(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  for (size_t i = 0; i < kGlyphCount; i++) {
    if (kFont[i].c == c) return &kFont[i];
  }
  return nullptr;
}

}  // namespace

int height(int scale) { return kGlyphH * scale; }

int width(const char *s, int scale) {
  const int n = (int)strlen(s);
  if (n == 0) return 0;
  // One blank column between glyphs, none trailing.
  return n * kGlyphW * scale + (n - 1) * scale;
}

void draw(int x, int y, const char *s, int scale, uint16_t color) {
  const int step = (kGlyphW + 1) * scale;
  for (int i = 0; s[i]; i++) {
    const Glyph *g = find(s[i]);
    if (g) {
      for (int r = 0; r < kGlyphH; r++) {
        const uint8_t bits = g->row[r];
        // Coalesce horizontally adjacent lit pixels into one rectangle. Three
        // pixels wide is not much, but at scale 8 that is one transaction
        // instead of three, and a clock redraws every second.
        int run = 0;
        for (int c = 0; c <= kGlyphW; c++) {
          const bool on = c < kGlyphW && (bits & (1 << (kGlyphW - 1 - c)));
          if (on) {
            run++;
            continue;
          }
          if (run) {
            display::fill_rect(x + i * step + (c - run) * scale, y + r * scale,
                               run * scale, scale, color);
            run = 0;
          }
        }
      }
    }
  }
}

void draw_centered(int y, const char *s, int scale, uint16_t color) {
  draw((LCD_W - width(s, scale)) / 2, y, s, scale, color);
}

}  // namespace text
