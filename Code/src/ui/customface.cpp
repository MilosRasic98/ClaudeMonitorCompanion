#include "customface.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "display.h"
#include "pins.h"
#include "tuning.h"

namespace customface {
namespace {

Preferences s_prefs;
uint8_t s_data[kTotalBytes];

// Four cells per byte, most significant bits first.
inline uint8_t cell_at(int frame, int i) {
  const int idx = frame * kFrameBytes + (i >> 2);
  const int shift = 6 - 2 * (i & 3);
  return (uint8_t)((s_data[idx] >> shift) & 0x3);
}

uint16_t colour_of(uint8_t v, uint16_t bg) {
  switch (v) {
    case kInk: return kBlack;
    case kLit: return kWhite;
    default:   return bg;
  }
}

bool frame_used(int frame) {
  const uint8_t *p = s_data + frame * kFrameBytes;
  for (int i = 0; i < kFrameBytes; i++) {
    if (p[i]) return true;
  }
  return false;
}

}  // namespace

void begin() {
  s_prefs.begin("face", false);
  memset(s_data, 0, sizeof(s_data));
  s_prefs.getBytes("bmp", s_data, sizeof(s_data));
}

bool store(const uint8_t *in, size_t len) {
  if (len != (size_t)kTotalBytes) return false;
  memcpy(s_data, in, kTotalBytes);
  s_prefs.putBytes("bmp", s_data, kTotalBytes);
  return true;
}

const uint8_t *data() { return s_data; }
bool has_drawing() { return frame_used(0); }
bool has_blink() { return frame_used(1); }

void draw(int frame, uint16_t bg, bool full) {
  const int other = frame ^ 1;

  for (int row = 0; row < kH; row++) {
    int run_start = -1;
    uint16_t run_c = 0;

    // One past the right edge so a run reaching the edge still gets flushed.
    for (int col = 0; col <= kW; col++) {
      bool want = false;
      uint16_t c = 0;

      if (col < kW) {
        const int i = row * kW + col;
        const uint8_t v = cell_at(frame, i);
        // A blink only differs from the resting face around the eyes. Drawing
        // just those cells turns a 672-rectangle repaint into a handful.
        if (full || v != cell_at(other, i)) {
          c = colour_of(v, bg);
          want = true;
        }
      }

      if (run_start >= 0 && (!want || c != run_c)) {
        display::fill_rect(run_start * kCell, row * kCell,
                           (col - run_start) * kCell, kCell, run_c);
        run_start = -1;
      }
      if (want && run_start < 0) {
        run_start = col;
        run_c = c;
      }
    }
  }
}

}  // namespace customface
