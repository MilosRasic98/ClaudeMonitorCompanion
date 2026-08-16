#pragma once
#include <stdint.h>

// Everything that decides how the face looks and feels. One edit and a reflash.
// Nothing in here is final — the eye geometry in particular is guesswork until
// the 3D-printed shell's screen cutout is measured.

// ---------------------------------------------------------------- palette --
// RGB565, big-endian, because the ST7789 wants big-endian and the ESP32 is
// little-endian. Pre-swapped once here so every fill is free.
#define RGB565(r, g, b) \
  ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define SWAP16(c) ((uint16_t)(((c) >> 8) | ((c) << 8)))
#define COLOR(r, g, b) SWAP16(RGB565(r, g, b))

// The on-screen orange will not match the filament orange by hex — one is
// emissive, the other diffuse. These are candidates to eyeball against the
// actual printed shell; the calibration mode picks between them.
static const uint16_t kOrangeCandidates[] = {
    COLOR(0xF0, 0x62, 0x0C),  // 0 saturated, close to the print
    COLOR(0xFF, 0x6A, 0x00),  // 1 brighter, glows harder
    COLOR(0xE0, 0x55, 0x08),  // 2 deeper, less shouty
    COLOR(0xFF, 0x7A, 0x1A),  // 3 lighter, more amber
};
#define ORANGE_DEFAULT 0

static const uint16_t kBlack = COLOR(0x00, 0x00, 0x00);
static const uint16_t kWhite = COLOR(0xFF, 0xFF, 0xFF);  // lens glints only

// The whole field goes red when the mascot wants something: an unacknowledged
// "needs input", or when it has been poked once too often. Visible across a
// room, which is the job the mechanical bell will eventually share.
static const uint16_t kRed = COLOR(0xC8, 0x14, 0x10);

// ------------------------------------------------------------------- grid --
// Quantum sets how many distinct steps a blink has. At 5 px a ~100 px eye gets
// ~20 steps, which reads as deliberate; 10 px gives ~10, which reads as janky.
#define GRID_Q 5

// -------------------------------------------------------------------- eyes --
// Two tall narrow bars, roughly 1:3, spaced about two eye-widths, slightly
// above centre. Matches the printed mascot's default face.
#define EYE_W        40
#define EYE_H        100
#define EYE_GAP      60   // gap between the inner edges
#define EYE_CENTER_Y 130  // vertical centre of both eyes

// --------------------------------------------------------------- styles --
// The printed mascot exists in several faces — bare eyes, a >< grin, round
// spectacles, chunky pixel glasses, deal-with-it shades. Swipe cycles them.
// Geometry below is relative to the eye positions above, so moving the eyes
// moves the accessories with them.

#define LENS_R        34   // round spectacles, outer radius
#define LENS_T        5    // ring thickness — one quantum reads best
#define ROUND_EYE_W   20   // eyes shrink to pupils inside the lenses
#define ROUND_EYE_H   30

#define GLASS_W       74   // rectangular lens, pixel glasses and shades
#define GLASS_H       44
#define GLASS_BRIDGE  6    // bridge bar thickness
#define GLINT_Q       2    // white highlight block size, in quanta

#define MOUTH_Y       205  // centre line of whatever mouth a style has
#define MOUTH_R       34   // smile arc radius
#define GRILL_W       70   // grill mouth
#define GRILL_H       30
#define STACHE_R      40   // moustache arc radius
#define STACHE_Y      196

// ------------------------------------------------------------- animation --
#define BLINK_CLOSE_MS   90   // full open to shut
#define BLINK_HOLD_MS    40   // shut
#define BLINK_OPEN_MS    120  // shut back to full open
#define BLINK_MIN_GAP_MS 2000
#define BLINK_MAX_GAP_MS 6000

#define FRAME_PERIOD_MS 16  // ~60 fps

// Per-mood motion. All amplitudes are in grid quanta, not pixels, so changing
// GRID_Q rescales the whole face coherently.
#define BORED_OPEN_PCT   60   // eyelid height while bored, % of EYE_H
#define BORED_LOOK_Q     2    // how far the bored gaze drifts aside
#define BORED_DROOP_Q    2    // and downward
#define WORKING_SWEEP_Q  3    // scan amplitude while Claude is running
#define WORKING_SWEEP_MS 1400 // one full left-right-left sweep
#define EXCITED_BOUNCE_Q 3    // vertical bounce amplitude
#define EXCITED_BOUNCE_MS 380 // one bounce cycle
#define CONFUSED_SHAKE_Q 1
#define CONFUSED_SHAKE_MS 700
#define SLEEP_BREATH_Q   1
#define SLEEP_BREATH_MS  3600
#define CHEVRON_STEPS    6    // rows in the >< excited face
#define WAKE_MS          700  // lids opening after sleep

// ------------------------------------------------------------------ moods --
// Thresholds. BORED and ASLEEP are pure elapsed time — no hook can report the
// absence of activity, so the board has to track it.
#define BORED_AFTER_MS   (5UL * 60UL * 1000UL)
#define SLEEP_AFTER_MS   (30UL * 60UL * 1000UL)

// A turn that never reports finishing must not strand the face in WORKING.
#define WORKING_MAX_MS   (10UL * 60UL * 1000UL)

// How long a "turn finished" celebration lasts. A "needs input" alert is
// sticky instead — it waits to be acknowledged.
#define EXCITED_MS       6000UL

// A sticky alert waits to be acknowledged, but not forever. If the clearing
// event is ever missed, this stops the face being stranded red.
#define ALERT_MAX_MS     (3UL * 60UL * 1000UL)
#define CONFUSED_MS      12000UL

// ----------------------------------------------------------------- energy --
// 0..100, bumped per event and decaying continuously. This is the "how hard is
// Claude being used" signal; it modulates motion within a mood rather than
// switching moods.
#define ENERGY_BUMP_PROMPT 22
#define ENERGY_BUMP_EVENT  10
#define ENERGY_DECAY_MS    4000  // one point lost per this many ms
#define ENERGY_MAX         100

// ------------------------------------------------------------------- wifi --
// The ESP32 gives up on its own after enough failed associations, so retries
// are supervised here rather than left to setAutoReconnect(). They back off:
// hammering an access point that is rate-limiting or temporarily blacklisting
// a MAC after failed auths is how a transient problem becomes a permanent one.
#define WIFI_RETRY_MS     8000
#define WIFI_RETRY_MAX_MS 240000

// Outbound reachability probe, for telling "the board is offline" apart from
// "the AP will not pass traffic between its clients". Dev aid only.
#define PROBE_HOST "192.168.1.253"
#define PROBE_PORT 8765

// ------------------------------------------------------------------ touch --
// Swipes are derived from the coordinate stream rather than read from the
// CST816's own gesture register. Two reasons: the chip only reports a gesture
// once the finger lifts, and it latches the code, so two swipes the same way in
// a row look identical and the second gets swallowed — which is precisely what
// cycling through styles does.
#define SWIPE_MIN_PX 35   // travel before a drag counts as a swipe
#define TAP_MAX_PX   18   // movement still loose enough to call a tap
#define TAP_MAX_MS   600
#define SWIPE_INVERT 0    // flip which direction advances the style

// Poking. A tap makes the eyes shake it off; keep poking and it gets annoyed.
#define POKE_SHAKE_MS     420  // how long the head-shake lasts
#define POKE_SHAKE_PER_MS 105  // one left-right cycle
#define POKE_SHAKE_Q      2    // shake amplitude, in grid quanta
#define POKE_ANGRY_COUNT  5    // taps within the window before it snaps
#define POKE_WINDOW_MS    4000
#define ANGRY_MS          7000  // how long it stays cross

// How far a drawn face travels, in whole cells. The art is ten-pixel blocks, so
// one cell is a ten-pixel step — small numbers go a long way.
#define CUSTOM_DRIFT_CELLS  1  // idle wander
#define CUSTOM_SWEEP_CELLS  2  // scanning while Claude works
#define CUSTOM_BOUNCE_CELLS 2  // excited
#define CUSTOM_SHAKE_CELLS  1  // confused, and shaking off a poke

// The usage-limit face: a big exclamation mark across the red field. Blinks,
// because a static red screen reads as a crash rather than a message.
#define BANG_BAR_W    30
#define BANG_BAR_H   120
#define BANG_TOP      45
#define BANG_DOT      30
#define BANG_DOT_GAP  25
#define BANG_BLINK_MS 620

// The angry face is one fixed set of slanted brows — no animation. Inner ends
// sit lower than outer ends, which is what reads as angry.
#define ANGRY_DROP  40  // total vertical fall across the eye width
#define ANGRY_THICK 20

// ------------------------------------------------------------------- bell --
// Placeholders. A solenoid or striker needs enough current to actually move,
// and a duty that does not simply cook the coil — these get tuned against the
// real actuator, not guessed here.
#define BELL_PWM_HZ       200
#define BELL_STRIKE_DUTY  200  // of 255
#define BELL_STRIKE_MS    40   // pulse length; released by bell::tick()

// Celebration on the onboard piezo when a turn finishes. Two rising chirps.
// Safe despite the "hold the piezo low" rule: the danger is the pin idling high
// or floating, and this drives PWM for a fifth of a second then puts it back
// low. A passive buzzer being briefly driven is what it is for.
#define PIEZO_BEEP1_HZ 2700
#define PIEZO_BEEP1_MS 70
#define PIEZO_GAP_MS   55
#define PIEZO_BEEP2_HZ 3500
#define PIEZO_BEEP2_MS 110
#define PIEZO_DUTY     128  // of 255; a square wave is loudest near half

// The whole pattern repeats, so it lands as "buzz buzz" rather than a single
// flourish. Set PIEZO_REPEATS to 1 for one, or raise it if you want more.
// The long buzz, for things that have gone wrong. Deliberately a different
// shape from the celebration, not just a different pitch: you should be able to
// tell good news from bad with your back to the desk.
#define PIEZO_LONG_HZ 1500
#define PIEZO_LONG_MS 650

#define PIEZO_REPEATS       2
#define PIEZO_REPEAT_GAP_MS 130  // longer than the inner gap, so the two read
                                 // as separate buzzes rather than four beeps

// --------------------------------------------------------------- backlight --
#define BACKLIGHT_SLEEPING 40
#define BACKLIGHT_BORED    110
#define BACKLIGHT_NORMAL   190
#define BACKLIGHT_EXCITED  255

// ---------------------------------------------------------------- display --
// 80 MHz is a 28% overclock past the ST7789's 16 ns write-cycle spec, but it is
// what the field runs on this panel. Drop to 40 MHz if artifacts appear at
// temperature inside the shell — there is no step in between.
#define LCD_SPI_HZ (80 * 1000 * 1000)

#define BACKLIGHT_DUTY_IDLE 140  // of 255
