#include "styles.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "display.h"
#include "pins.h"
#include "tuning.h"

namespace face {
namespace {

inline int gq()  { return config::v::grid_q(); }
inline int ew()  { return config::v::eye_w(); }
inline int ecy() { return config::v::eye_center_y(); }

inline int eye_cx(int eye) {
  const int gap = config::v::eye_gap();
  return eye == 0 ? (LCD_W - gap) / 2 - ew() / 2 : (LCD_W + gap) / 2 + ew() / 2;
}
const int kFaceCx = LCD_W / 2;

// Everything snaps to the grid. Curves included — a circle drawn on a coarse
// grid is a staircase, which is exactly the printed mascot's look.
int snap(float v) { return (int)(floorf(v / gq()) * gq()); }

void span(int x, int y, int w, int h, uint16_t color) {
  const int x0 = snap((float)x);
  const int x1 = snap((float)(x + w) + gq() - 1);
  display::fill_rect(x0, y, x1 - x0, h, color);
}

// A ring, one grid row at a time: each row contributes either two short spans
// (left and right of the hole) or one long span where the hole has closed.
// ymin/ymax clip it, which is how the smile and moustache are drawn as arcs.
void ring(int cx, int cy, int r_out, int thick, int ymin, int ymax,
          uint16_t color) {
  const int r_in = r_out - thick;
  for (int y = snap((float)(cy - r_out)); y < cy + r_out; y += gq()) {
    if (y + gq() <= ymin || y >= ymax) continue;
    const float dy = (float)(y + gq() * 0.5f) - cy;
    const float outer = (float)r_out * r_out - dy * dy;
    if (outer <= 0.0f) continue;
    const int ox = (int)sqrtf(outer);
    const float inner = (float)r_in * r_in - dy * dy;
    if (inner <= 0.0f) {
      span(cx - ox, y, 2 * ox, gq(), color);
    } else {
      const int ix = (int)sqrtf(inner);
      span(cx - ox, y, ox - ix, gq(), color);
      span(cx + ix, y, ox - ix, gq(), color);
    }
  }
}

// The white highlight blocks on the lenses. Two stepped blocks reading as a
// reflection, the way pixel-art sunglasses always do it.
void glints(int lens_x, int lens_y) {
  const int b = GLINT_Q * gq();
  display::fill_rect(lens_x + gq(), lens_y + gq(), b, b, kWhite);
  display::fill_rect(lens_x + gq() + b, lens_y + gq() + b, b, b, kWhite);
}

void rect_lenses(int dx, int dy, int lens_h, uint16_t orange) {
  const int y = snap((float)(ecy() - lens_h / 2 + dy));
  for (int i = 0; i < 2; i++) {
    const int x = snap((float)(eye_cx(i) - GLASS_W / 2 + dx));
    display::fill_rect(x, y, GLASS_W, lens_h, kBlack);
    glints(x, y);
  }
  // Bridge, joining the two lenses across the nose.
  const int bx = snap((float)(eye_cx(0) + GLASS_W / 2 + dx));
  const int bw = snap((float)(eye_cx(1) - GLASS_W / 2 + dx)) - bx;
  display::fill_rect(bx, snap((float)(ecy() - GLASS_BRIDGE / 2 + dy)), bw,
                     GLASS_BRIDGE, kBlack);
  (void)orange;
}

// The barred mouth on the spectacled face: a black block with orange slits.
void grill(int dx, int dy, uint16_t orange) {
  const int x = snap((float)(kFaceCx - GRILL_W / 2 + dx));
  const int y = snap((float)(MOUTH_Y - GRILL_H / 2 + dy));
  display::fill_rect(x, y, GRILL_W, GRILL_H, kBlack);
  for (int sx = x + 2 * gq(); sx < x + GRILL_W - gq(); sx += 2 * gq()) {
    display::fill_rect(sx, y + gq(), gq(), GRILL_H - 2 * gq(), orange);
  }
}

void smile(int dx, int dy) {
  const int cy = MOUTH_Y + dy;
  ring(kFaceCx + dx, cy, MOUTH_R, LENS_T, cy, cy + MOUTH_R, kBlack);
}

// Two mirrored upper arcs meeting in the middle, with the outer ends carried
// downward — a chunky handlebar.
void moustache(int dx, int dy) {
  const int cy = STACHE_Y + dy;
  const int half = STACHE_R;
  for (int i = 0; i < 2; i++) {
    const int cx = kFaceCx + dx + (i == 0 ? -half / 2 : half / 2);
    ring(cx, cy, half, LENS_T + gq(), cy - half, cy, kBlack);
  }
  display::fill_rect(snap((float)(kFaceCx + dx - gq())), snap((float)(cy - gq())),
                     2 * gq(), 2 * gq(), kBlack);
}

const StyleInfo kStyles[(int)Style::Count] = {
    // name       eyes  chevron  eye_w         eye_h         travel  acc
    {"plain",     true,  false,  0,            0,            3,      false},
    {"grin",      true,  true,   0,            0,            3,      false},
    {"round",     true,  false,  ROUND_EYE_W,  ROUND_EYE_H,  1,      true},
    {"pixel",     false, false,  0,            0,            0,      true},
    {"shades",    false, false,  0,            0,            0,      true},
    // The custom faces are drawn wholesale by customface.cpp; the eye machinery
    // and the accessory machinery both sit out.
    {"custom1",   false, false,  0,            0,            0,      false},
    {"custom2",   false, false,  0,            0,            0,      false},
    {"custom3",   false, false,  0,            0,            0,      false},
    {"custom4",   false, false,  0,            0,            0,      false},
};

}  // namespace

int custom_slot(Style s) {
  const int i = (int)s - (int)Style::Custom0;
  return (i >= 0 && i < (int)Style::Count - (int)Style::Custom0) ? i : -1;
}

const StyleInfo &style_info(Style s) {
  const int i = (int)s;
  return kStyles[(i >= 0 && i < (int)Style::Count) ? i : 0];
}

void draw_accessories(Style s, int dx, int dy, uint16_t orange) {
  switch (s) {
    case Style::Round:
      for (int i = 0; i < 2; i++) {
        ring(eye_cx(i) + dx, ecy() + dy, LENS_R, LENS_T, 0, LCD_H, kBlack);
      }
      display::fill_rect(
          snap((float)(eye_cx(0) + LENS_R + dx)),
          snap((float)(ecy() - LENS_T / 2 + dy)),
          snap((float)(eye_cx(1) - LENS_R + dx)) - snap((float)(eye_cx(0) + LENS_R + dx)),
          LENS_T, kBlack);
      grill(dx, dy, orange);
      break;

    case Style::Pixel:
      rect_lenses(dx, dy, GLASS_H, orange);
      smile(dx, dy);
      break;

    case Style::Shades:
      // Shorter, meaner lenses than the pixel glasses, plus the handlebar.
      rect_lenses(dx, dy, GLASS_H - 3 * gq(), orange);
      moustache(dx, dy);
      break;

    default:
      break;
  }
}

void accessory_box(Style s, int dx, int dy, int &x, int &y, int &w, int &h) {
  x = 0;
  w = LCD_W;
  switch (s) {
    case Style::Round:
      y = ecy() - LENS_R + dy - gq();
      h = (MOUTH_Y + GRILL_H / 2 + dy) - y + gq();
      break;
    case Style::Pixel:
      y = ecy() - GLASS_H / 2 + dy - gq();
      h = (MOUTH_Y + MOUTH_R + dy) - y + gq();
      break;
    case Style::Shades:
      y = ecy() - GLASS_H / 2 + dy - gq();
      h = (STACHE_Y + dy) - y + gq();
      break;
    default:
      x = y = w = h = 0;
      break;
  }
}

}  // namespace face
