#pragma once
#include <stdint.h>

// The faces the printed mascot comes in. A style is a costume; the mood is the
// expression underneath it. Both apply at once — a bored mascot in shades is
// still bored, it just shows it with the glasses rather than the eyes.

namespace face {

enum class Style : uint8_t {
  Plain,   // two bar eyes, nothing else — the default character
  Grin,    // >< chevrons, always
  Round,   // round spectacles with pupils inside, grill mouth
  Pixel,   // chunky rectangular glasses with glints, open smile
  Shades,  // deal-with-it sunglasses and a moustache
  // Four slots the user draws on the settings page. Empty ones are skipped when
  // swiping, so they are never dead screens.
  Custom0,
  Custom1,
  Custom2,
  Custom3,
  Count,
};

struct StyleInfo {
  const char *name;
  bool eyes_visible;   // false when the lenses are opaque
  bool force_chevron;  // the >< face regardless of mood
  int16_t eye_w;       // 0 = use EYE_W / EYE_H from tuning.h
  int16_t eye_h;
  int8_t travel_q;     // how far the eyes may wander, in grid quanta
  bool has_accessories;
};

const StyleInfo &style_info(Style s);

// -1 for the built-in styles, otherwise the custom slot index.
int custom_slot(Style s);

// Static art for a style, drawn once when the style or its offset changes.
void draw_accessories(Style s, int dx, int dy, uint16_t orange);

// Footprint of that art, so the previous position can be erased when it moves.
void accessory_box(Style s, int dx, int dy, int &x, int &y, int &w, int &h);

}  // namespace face
