#include "face.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"
#include "customface.h"
#include "display.h"
#include "pins.h"
#include "tuning.h"

namespace face {
namespace {

using mood::Mood;

struct Rect {
  int16_t x, y, w, h;
  bool empty() const { return w <= 0 || h <= 0; }
};

enum Shape : uint8_t { kBar, kChevron, kAngry };

struct EyeState {
  int16_t h;      // open height, px
  int16_t dx, dy; // offset from rest, px
  Shape shape;
  bool operator==(const EyeState &o) const {
    return h == o.h && dx == o.dx && dy == o.dy && shape == o.shape;
  }
};

// Geometry is configurable at runtime, so what used to be constants are now
// one-line accessors. They are called several times per frame; at 240 MHz the
// cost is lost in the noise next to a single SPI transaction.
inline int gq()  { return config::v::grid_q(); }
inline int ew()  { return config::v::eye_w(); }
inline int eh()  { return config::v::eye_h(); }
inline int ecy() { return config::v::eye_center_y(); }

inline int eye_rest_x(int eye) {
  const int gap = config::v::eye_gap();
  return eye == 0 ? (LCD_W - gap) / 2 - ew() : (LCD_W + gap) / 2;
}

uint8_t s_orange_idx = ORANGE_DEFAULT;
uint16_t s_orange = kOrangeCandidates[ORANGE_DEFAULT];
uint16_t s_bg = kOrangeCandidates[ORANGE_DEFAULT];
const uint8_t kOrangeCount =
    sizeof(kOrangeCandidates) / sizeof(kOrangeCandidates[0]);

Style s_style = Style::Plain;

EyeState s_prev[2];
Rect s_prev_box[2];
bool s_first_frame = true;
uint8_t s_backlight = 0;

// Accessories are static art: drawn when the style changes, and again only if
// the mood shifts them. Tracking the last offset is what lets the old position
// be erased without repainting the whole screen.
int16_t s_acc_dx = 0, s_acc_dy = 0;
bool s_acc_drawn = false;

// Style and colour changes arrive from loop() — touch gestures and serial keys
// — but drawing may only happen in the render task: esp_lcd panel operations
// are not thread-safe, and two tasks issuing SPI transactions at once corrupts
// the display. So requests are queued here and applied at the top of a frame.
volatile int8_t s_want_style_delta = 0;
volatile bool s_want_next_orange = false;
volatile bool s_want_reload = false;

// Set by poke(), which is called from loop(). Only ever read here.
volatile uint32_t s_poke_until = 0;

// The limit face owns the whole screen while it is up.
bool s_limit_active = false;
bool s_limit_on = false;

// The user-drawn face, and which of its two frames is currently on screen.
bool s_custom_drawn = false;
int s_custom_frame = 0;
int s_custom_slot = -1;
int s_custom_dx = 0, s_custom_dy = 0;

// Blink is an overlay on whatever the mood is doing, not a mood of its own.
uint32_t s_blink_at = 0;
uint32_t s_blink_started = 0;
bool s_blinking = false;

// --------------------------------------------------------------- helpers --

// Snap to the grid. Motion stepping in visible increments is the whole point of
// the pixel-art look, so this is applied to every coordinate, not just sizes.
int16_t q(float v) {
  return (int16_t)(lroundf(v / (float)gq()) * gq());
}

float wave(uint32_t now, uint32_t period_ms) {
  if (period_ms == 0) return 0.0f;
  return sinf((float)(now % period_ms) / (float)period_ms * 2.0f * (float)PI);
}

// Fill the part of `a` that `b` does not cover. Used both ways round: erase the
// pixels the eye vacated, then paint only the pixels it newly occupies. Cheaper
// than repainting the whole eye box every frame, and it cannot flicker.
void fill_diff(const Rect &a, const Rect &b, uint16_t color) {
  if (a.empty()) return;

  const int ax2 = a.x + a.w, ay2 = a.y + a.h;
  const int bx2 = b.x + b.w, by2 = b.y + b.h;

  if (b.empty() || a.x >= bx2 || ax2 <= b.x || a.y >= by2 || ay2 <= b.y) {
    display::fill_rect(a.x, a.y, a.w, a.h, color);
    return;
  }

  const int oy1 = max((int)a.y, (int)b.y);
  const int oy2 = min(ay2, by2);

  if (a.y < b.y) display::fill_rect(a.x, a.y, a.w, b.y - a.y, color);
  if (ay2 > by2) display::fill_rect(a.x, by2, a.w, ay2 - by2, color);
  if (a.x < b.x) display::fill_rect(a.x, oy1, b.x - a.x, oy2 - oy1, color);
  if (ax2 > bx2) display::fill_rect(bx2, oy1, ax2 - bx2, oy2 - oy1, color);
}

Rect box_of(int eye, const EyeState &s) {
  const StyleInfo &info = style_info(s_style);
  Rect r;
  r.w = info.eye_w ? info.eye_w : ew();
  r.h = (s.shape == kAngry) ? (ANGRY_DROP + ANGRY_THICK) : s.h;
  r.x = (int16_t)(eye_rest_x(eye) + (ew() - r.w) / 2 + s.dx);
  r.y = (int16_t)(ecy() - r.h / 2 + s.dy);
  return r;
}

// The >< face from the printed mascot: a stack of quantised blocks stepping out
// and back. Left eye points right, right eye points left, so they face inward.
void draw_chevron(int eye, const Rect &box) {
  const int steps = CHEVRON_STEPS;
  const int row_h = max(gq(), (int)q((float)box.h / steps));
  const int block_w = 2 * gq();
  const float mid = (steps - 1) / 2.0f;
  const float travel = (float)(box.w - block_w);

  for (int i = 0; i < steps; i++) {
    const float d = fabsf((float)i - mid);
    const float t = (eye == 0) ? (mid - d) : d;  // 0 = '>' , 1 = '<'
    const int x = box.x + q(travel * (mid > 0 ? t / mid : 0.0f));
    display::fill_rect(x, box.y + i * row_h, block_w, row_h, kBlack);
  }
}

// The angry face: a thick brow slanting down towards the nose, stepped along
// the grid. Left falls left-to-right, right rises, so the inner ends are lower.
// Deliberately static — one set face, no animation.
void draw_angry(int eye, const Rect &box) {
  const int steps = box.w / gq();
  if (steps <= 0) return;
  const int drop = q((float)ANGRY_DROP / steps);
  for (int i = 0; i < steps; i++) {
    const int step = (eye == 0) ? i : (steps - 1 - i);
    display::fill_rect(box.x + i * gq(), box.y + step * (drop ? drop : gq()),
                       gq(), ANGRY_THICK, kBlack);
  }
}

void draw_eye(int eye, const EyeState &next) {
  if (!s_first_frame && s_prev[eye] == next) return;

  const Rect box = box_of(eye, next);

  if (next.shape == kBar && s_prev[eye].shape == kBar && !s_first_frame) {
    // Two axis-aligned rectangles, so only the difference has to move. Any
    // other shape falls through to the wholesale clear below.
    fill_diff(s_prev_box[eye], box, s_bg);
    fill_diff(box, s_prev_box[eye], kBlack);
  } else {
    // Shape changed, or first paint. Clear the old footprint wholesale — this
    // happens on mood transitions, not every frame.
    if (!s_first_frame && !s_prev_box[eye].empty()) {
      display::fill_rect(s_prev_box[eye].x, s_prev_box[eye].y,
                         s_prev_box[eye].w, s_prev_box[eye].h, s_bg);
    }
    if (next.shape == kChevron) {
      draw_chevron(eye, box);
    } else if (next.shape == kAngry) {
      draw_angry(eye, box);
    } else {
      display::fill_rect(box.x, box.y, box.w, box.h, kBlack);
    }
  }

  s_prev[eye] = next;
  s_prev_box[eye] = box;
}

// Where a drawn face sits this frame, in whole cells. The same motions the
// parametric faces perform with geometry, done by moving the whole bitmap:
// nothing here changes the artwork, only where it is.
void custom_offset(Mood m, uint8_t energy, uint32_t now, uint32_t in_mood,
                   int &cx, int &cy) {
  cx = 0;
  cy = 0;
  switch (m) {
    case Mood::Sleeping:
      cy = (int)lroundf(wave(now, SLEEP_BREATH_MS) * 1.0f);
      break;
    case Mood::Chill:
      cx = (int)lroundf(wave(now, 5200) * CUSTOM_DRIFT_CELLS);
      break;
    case Mood::Bored:
      cx = -CUSTOM_DRIFT_CELLS;
      cy = CUSTOM_DRIFT_CELLS;
      break;
    case Mood::Working: {
      const float speed = 1.0f + (energy / 100.0f);
      cx = (int)lroundf(wave((uint32_t)(now * speed), WORKING_SWEEP_MS) *
                        CUSTOM_SWEEP_CELLS);
      break;
    }
    case Mood::Excited:
      cy = -(int)lroundf(fabsf(wave(now, EXCITED_BOUNCE_MS)) *
                         CUSTOM_BOUNCE_CELLS);
      break;
    case Mood::Confused:
      cx = (int)lroundf(wave(now, CONFUSED_SHAKE_MS) * CUSTOM_SHAKE_CELLS);
      break;
    default:
      break;
  }
  (void)in_mood;

  // A poke is shaken off whatever the mood, same as the parametric faces.
  if (now < s_poke_until) {
    cx += (int)lroundf(wave(now, POKE_SHAKE_PER_MS) * CUSTOM_SHAKE_CELLS);
  }
}

// The usage-limit face. Not eyes at all — a single exclamation mark centred on
// the screen, blinking slowly so it reads as a message rather than a crash.
// Drawn and erased as one block, which is why it bypasses the eye machinery.
void draw_bang(bool visible) {
  const uint16_t c = visible ? kBlack : s_bg;
  const int cx = LCD_W / 2;
  display::fill_rect(cx - BANG_BAR_W / 2, BANG_TOP, BANG_BAR_W, BANG_BAR_H, c);
  display::fill_rect(cx - BANG_DOT / 2, BANG_TOP + BANG_BAR_H + BANG_DOT_GAP,
                     BANG_DOT, BANG_DOT, c);
}

// ---------------------------------------------------------------- blinks --

// Energy shortens the gap between blinks: a busy session looks more awake than
// a quiet one even when both are nominally in the same mood.
uint32_t blink_gap(uint8_t energy, float stretch) {
  const float t = energy / 100.0f;
  const float base = config::v::blink_max_ms() - t * (config::v::blink_max_ms() - config::v::blink_min_ms());
  return (uint32_t)(base * stretch);
}

// Returns 0..1, where 1 is fully open.
float blink_openness(uint32_t now, uint8_t energy, float stretch) {
  const uint32_t close_ms = (uint32_t)(BLINK_CLOSE_MS * stretch);
  const uint32_t open_ms = (uint32_t)(BLINK_OPEN_MS * stretch);
  const uint32_t total = close_ms + BLINK_HOLD_MS + open_ms;

  if (!s_blinking) {
    if (s_blink_at == 0) s_blink_at = now + blink_gap(energy, stretch);
    if (now < s_blink_at) return 1.0f;
    s_blinking = true;
    s_blink_started = now;
  }

  const uint32_t t = now - s_blink_started;
  if (t >= total) {
    s_blinking = false;
    s_blink_at = now + blink_gap(energy, stretch);
    return 1.0f;
  }
  if (t < close_ms) return 1.0f - (float)t / close_ms;
  if (t < close_ms + BLINK_HOLD_MS) return 0.0f;
  return (float)(t - close_ms - BLINK_HOLD_MS) / open_ms;
}

// ------------------------------------------------------------ mood shapes --

// How much a mood stretches the blink. Zero means the mood does not blink.
float blink_stretch(Mood m) {
  switch (m) {
    case Mood::Chill:    return 1.0f;
    case Mood::Bored:    return 2.2f;  // long, slow, unimpressed
    case Mood::Working:  return 0.7f;
    case Mood::Confused: return 1.4f;
    case Mood::Limit:    return 0.0f;
    default:             return 0.0f;
  }
}

// Styles reshape whatever the mood produced: the >< grin overrides the eye
// shape outright, spectacles shrink the eyes to pupils and pen them inside the
// lenses, and opaque lenses stop the eyes travelling at all.
void apply_style(EyeState &s, const StyleInfo &info) {
  if (info.force_chevron) s.shape = kChevron;

  if (info.eye_h) {
    // Scale the mood's openness into the smaller eye rather than clamping it,
    // so a blink still reads as a blink at pupil size.
    const int16_t min_h = gq();
    const float t = (float)(s.h - min_h) / (float)(eh() - min_h);
    s.h = q(min_h + (info.eye_h - min_h) * (t < 0.0f ? 0.0f : t));
  }

  const int16_t limit = (int16_t)(info.travel_q * gq());
  s.dx = constrain(s.dx, (int16_t)-limit, limit);
  s.dy = constrain(s.dy, (int16_t)-limit, limit);
}

// `open` is computed once per frame by the caller and shared by both eyes. It
// has to be: blink_openness() advances a state machine, so calling it per eye
// would let the two eyes drift into different blink phases.
EyeState compute(int eye, Mood m, uint8_t energy, uint32_t now,
                 uint32_t in_mood, float open) {
  EyeState s = {};
  s.shape = kBar;
  s.h = eh();

  // Openness never goes fully to zero: the printed mascot's closed eye is a
  // flat bar, not an absence.
  const int16_t min_h = gq();

  switch (m) {
    case Mood::Sleeping:
      s.h = min_h;
      s.dy = q(wave(now, SLEEP_BREATH_MS) * SLEEP_BREATH_Q * gq());
      return s;

    case Mood::Waking: {
      const float t = min(1.0f, (float)in_mood / (float)WAKE_MS);
      // Slight overshoot so the lids snap open rather than easing politely.
      const float e = 1.0f - powf(1.0f - t, 3.0f);
      const float over = 1.0f + 0.12f * sinf(t * (float)PI);
      s.h = q(min_h + (eh() - min_h) * e * over);
      return s;
    }

    case Mood::Chill:
      s.dx = q(wave(now, 5200) * gq());
      break;

    case Mood::Bored:
      s.h = (int16_t)(eh() * BORED_OPEN_PCT / 100);
      s.dy = BORED_DROOP_Q * gq();
      s.dx = -BORED_LOOK_Q * gq();
      break;

    case Mood::Working: {
      const float speed = 1.0f + (energy / 100.0f);
      s.dx = q(wave((uint32_t)(now * speed), WORKING_SWEEP_MS) *
               WORKING_SWEEP_Q * gq());
      break;
    }

    case Mood::Excited: {
      s.shape = kChevron;
      const float amp = 1.0f + (energy / 100.0f) * 0.6f;
      s.dy = q(-fabsf(wave(now, EXCITED_BOUNCE_MS)) * EXCITED_BOUNCE_Q *
               gq() * amp);
      return s;
    }

    case Mood::Confused:
      // One eye squinting reads as scepticism far better than two.
      if (eye == 1) s.h = eh() / 2;
      s.dx = q(wave(now, CONFUSED_SHAKE_MS) * CONFUSED_SHAKE_Q * gq());
      break;

    case Mood::Limit:
      // Handled entirely by draw_bang(); the eyes are not drawn at all.
      return s;

    case Mood::Angry:
      s.shape = kAngry;
      s.h = ANGRY_DROP + ANGRY_THICK;
      return s;  // one set face — no blink, no drift, no shake
  }

  s.h = q(min_h + (s.h - min_h) * open);

  // A poke gets shaken off, whatever else is going on. Added on top of the
  // mood's own motion rather than replacing it.
  const uint32_t poke_until = s_poke_until;
  if (now < poke_until) {
    s.dx = (int16_t)(s.dx + q(wave(now, POKE_SHAKE_PER_MS) * POKE_SHAKE_Q * gq()));
  }
  return s;
}

uint8_t backlight_for(Mood m) {
  switch (m) {
    case Mood::Sleeping: return config::v::backlight_sleeping();
    case Mood::Bored:    return config::v::backlight_bored();
    case Mood::Excited:
    case Mood::Angry:    return config::v::backlight_excited();
    default:             return config::v::backlight_normal();
  }
}

// Pulls style and colour from the config store rather than keeping a private
// copy, so the web page and the touchscreen cannot disagree about them.
void reload_from_config() {
  const int st = config::v::style();
  if (st >= 0 && st < (int)Style::Count) s_style = (Style)st;

  // A custom slot that has since been cleared would render as an empty field:
  // the custom styles draw no eyes of their own, and there is no drawing to put
  // in their place. Reachable by selecting a face and then erasing it, so fall
  // back rather than showing a blank panel.
  const int cs = custom_slot(s_style);
  if (cs >= 0 && !customface::has_drawing(cs)) {
    s_style = Style::Plain;
    config::set("style", (int32_t)s_style);
  }
  const int oi = config::v::orange();
  if (oi >= 0 && oi < (int)kOrangeCount) {
    s_orange_idx = (uint8_t)oi;
    s_orange = kOrangeCandidates[oi];
  }
  if (!mood::alerting()) s_bg = s_orange;
}

// Full repaint. Only ever called on a style or colour change, never per frame.
void repaint() {
  display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
  s_custom_drawn = false;
  customface::invalidate();
  if (style_info(s_style).has_accessories) {
    draw_accessories(s_style, s_acc_dx, s_acc_dy, s_bg);
    s_acc_drawn = true;
  } else {
    s_acc_drawn = false;
  }
  s_first_frame = true;  // eyes redraw wholesale on top
}

// Drains the requests queued by loop(). Runs inside the render task, so all
// drawing stays on one thread.
void apply_pending() {
  if (s_want_reload) {
    s_want_reload = false;
    reload_from_config();
    repaint();
  }
  const int8_t delta = s_want_style_delta;
  if (delta) {
    s_want_style_delta = (int8_t)(s_want_style_delta - delta);
    const int n = (int)Style::Count;
    const int step = delta > 0 ? 1 : -1;
    int next = (int)s_style;
    // Step past custom slots with nothing drawn in them. Bounded by n so an
    // empty roster cannot spin forever.
    for (int moved = 0; moved < abs((int)delta); moved++) {
      for (int guard = 0; guard < n; guard++) {
        next = ((next + step) % n + n) % n;
        const int cs = custom_slot((Style)next);
        if (cs < 0 || customface::has_drawing(cs)) break;
      }
    }
    s_style = (Style)next;
    config::set("style", (int32_t)s_style);
    Serial.printf("style : %s\n", style_name());
    s_acc_drawn = false;
    s_acc_dx = s_acc_dy = 0;
    repaint();
  }
  if (s_want_next_orange) {
    s_want_next_orange = false;
    s_orange_idx = (uint8_t)((s_orange_idx + 1) % kOrangeCount);
    s_orange = kOrangeCandidates[s_orange_idx];
    config::set("orange", s_orange_idx);
    if (!mood::alerting()) s_bg = s_orange;
    repaint();
  }
}

}  // namespace

void begin() {
  reload_from_config();
  s_bg = s_orange;
  repaint();
}

void refresh() { s_want_reload = true; }

void render(Mood m, uint8_t energy, uint32_t now) {
  apply_pending();

  // Red whenever the mascot actively wants something. Cheap to check, and the
  // repaint only happens on the transition.
  const uint16_t want_bg = mood::alerting() ? kRed : s_orange;
  if (want_bg != s_bg) {
    s_bg = want_bg;
    repaint();
  }

  const uint32_t in_mood = mood::in_mood_ms();
  const StyleInfo &info = style_info(s_style);

    // The custom face owns the whole panel: the user drew every cell, so there
  // is nothing for the eye or accessory code to add. It cannot change shape the
  // way the parametric faces do, but it can move, and moving carries most of
  // the expression — so it gets the same mood vocabulary by translation.
  const int slot = custom_slot(s_style);
  if (m != Mood::Limit && slot >= 0 && customface::has_drawing(slot)) {
    const float stretch = blink_stretch(m);
    const bool closed =
        customface::has_blink(slot) && stretch > 0.0f &&
        blink_openness(now, energy, stretch) < 0.5f;
    const int frame = closed ? 1 : 0;

    int cx = 0, cy = 0;
    custom_offset(m, energy, now, in_mood, cx, cy);

    if (!s_custom_drawn || frame != s_custom_frame || slot != s_custom_slot ||
        cx != s_custom_dx || cy != s_custom_dy) {
      if (!s_custom_drawn || slot != s_custom_slot) {
        display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
        customface::invalidate();
      }
      customface::draw(slot, frame, s_bg, cx, cy);
      s_custom_drawn = true;
      s_custom_frame = frame;
      s_custom_slot = slot;
      s_custom_dx = cx;
      s_custom_dy = cy;
      s_first_frame = true;   // eyes repaint wholesale if the style changes back
      s_acc_drawn = false;
    }
    const uint8_t bl = backlight_for(m);
    if (bl != s_backlight) {
      s_backlight = bl;
      display::backlight(bl);
    }
    return;
  }
  if (s_custom_drawn) {
    s_custom_drawn = false;
    customface::invalidate();
    display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
    s_first_frame = true;
    s_acc_drawn = false;
  }

  // The limit face replaces everything, so it short-circuits the eye and
  // accessory paths rather than layering on top of them.
  if (m == Mood::Limit) {
    const bool on = ((now / BANG_BLINK_MS) % 2) == 0;
    if (!s_limit_active) {
      display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
      s_limit_active = true;
      s_limit_on = !on;  // force the first draw below
      s_first_frame = true;  // eyes repaint wholesale when this mood ends
      s_acc_drawn = false;
    }
    if (on != s_limit_on) {
      s_limit_on = on;
      draw_bang(on);
    }
    const uint8_t bl = backlight_for(m);
    if (bl != s_backlight) {
      s_backlight = bl;
      display::backlight(bl);
    }
    return;
  }
  if (s_limit_active) {
    s_limit_active = false;
    display::fill_rect(0, 0, LCD_W, LCD_H, s_bg);
    s_first_frame = true;
    s_acc_drawn = false;
  }

  const float stretch = blink_stretch(m);
  const float open =
      stretch > 0.0f ? blink_openness(now, energy, stretch) : 1.0f;

  EyeState eyes[2];
  EyeState raw[2];
  for (int eye = 0; eye < 2; eye++) {
    raw[eye] = compute(eye, m, energy, now, in_mood, open);
    eyes[eye] = raw[eye];
    apply_style(eyes[eye], info);
  }

  // When the lenses are opaque the mood has to come through the accessories
  // instead of the eyes, so they inherit the bounce and the droop. Taken from
  // the unclamped motion: those styles pin the eyes still precisely so the
  // glasses can carry the movement instead.
  if (info.has_accessories) {
    const int16_t cap = 2 * gq();
    const int16_t adx =
        info.eyes_visible ? 0 : constrain(raw[0].dx, (int16_t)-cap, cap);
    const int16_t ady =
        info.eyes_visible ? 0 : constrain(raw[0].dy, (int16_t)-cap, cap);
    if (!s_acc_drawn || adx != s_acc_dx || ady != s_acc_dy) {
      int bx, by, bw, bh;
      if (s_acc_drawn) {
        accessory_box(s_style, s_acc_dx, s_acc_dy, bx, by, bw, bh);
        display::fill_rect(bx, by, bw, bh, s_bg);
      }
      s_acc_dx = adx;
      s_acc_dy = ady;
      draw_accessories(s_style, adx, ady, s_bg);
      s_acc_drawn = true;
      s_first_frame = true;  // the erase took the eyes with it
    }
  }

  if (info.eyes_visible) {
    for (int eye = 0; eye < 2; eye++) draw_eye(eye, eyes[eye]);
  }
  s_first_frame = false;

  const uint8_t bl = backlight_for(m);
  if (bl != s_backlight) {
    s_backlight = bl;
    display::backlight(bl);
  }
}

void poke() { s_poke_until = millis() + POKE_SHAKE_MS; }

void next_orange() { s_want_next_orange = true; }

uint8_t orange_index() { return s_orange_idx; }

void cycle_style(int delta) {
  s_want_style_delta = (int8_t)(s_want_style_delta + delta);
}

uint16_t background() { return mood::alerting() ? kRed : s_orange; }

Style style() { return s_style; }
const char *style_name() { return style_info(s_style).name; }

}  // namespace face
