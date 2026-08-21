#include "bell.h"

#include <Arduino.h>

#include "driver/ledc.h"

#include "config.h"
#include "pins.h"
#include "tuning.h"

namespace bell {
namespace {

// The backlight owns LEDC_TIMER_0. Channels that share a timer corrupt each
// other's frequency and duty resolution, so the striker gets its own.
const ledc_timer_t kBellTimer = LEDC_TIMER_1;
const ledc_channel_t kBellChannel = LEDC_CHANNEL_1;

// The piezo gets a third timer of its own, for the same reason the striker
// does: LEDC channels sharing a timer corrupt each other's frequency, and this
// one changes frequency mid-sequence.
const ledc_timer_t kPiezoTimer = LEDC_TIMER_2;
const ledc_channel_t kPiezoChannel = LEDC_CHANNEL_2;

bool s_wired = false;
bool s_attached = false;  // is LEDC currently driving the servo pin
int s_pin = -1;         // servo signal, resolved from config
int s_switch_pin = -1;  // ring sensor, -1 when stopping on time
uint32_t s_stop_at = 0; // hard deadline, always set
bool s_running = false;

// Ring counting. The striker closes the switch once per pass: the pin leaves
// idle as it swings through and returns as it clears, so a ring is the return.
// Counting the return rather than the departure means a striker that stops
// resting against the switch cannot inflate the count.
int s_rings_wanted = 0;
int s_rings_seen = 0;
bool s_left_idle = false;
int s_last_level = -1;
uint32_t s_last_edge_ms = 0;

// The ESP32-S3's LEDC tops out at 14-bit duty resolution -- unlike the original
// ESP32, which does 20. At 50 Hz that is 1.2 us per step, still far finer than
// any servo resolves.
const int kServoBits = 14;

uint32_t us_to_duty(int us) {
  if (us < 0) us = 0;
  if (us > SERVO_PERIOD_US) us = SERVO_PERIOD_US;
  return (uint32_t)((uint64_t)us * ((1u << kServoBits) - 1) / SERVO_PERIOD_US);
}

void servo_write_us(int us) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, kBellChannel, us_to_duty(us));
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kBellChannel);
}

bool switch_idle() {
  if (s_switch_pin < 0) return true;
  const int lvl = digitalRead(s_switch_pin);
  return config::v::bell_switch_invert() ? (lvl == LOW) : (lvl == HIGH);
}

// Release the servo rather than hold it at neutral.
//
// A continuous-rotation servo does not move without a signal, so cutting the
// PWM and parking the pin low is a complete stop that draws nothing. Holding it
// at a neutral pulse instead means it keeps drawing, and if the neutral trim is
// even slightly off it creeps forever -- current for no reason, in a sealed
// case, right next to the antenna.
void servo_release() {
  if (s_attached) {
    ledc_stop(LEDC_LOW_SPEED_MODE, kBellChannel, 0);
    s_attached = false;
  }
  if (s_pin >= 0) {
    pinMode(s_pin, OUTPUT);
    digitalWrite(s_pin, LOW);
  }
  s_running = false;
}

// Attach on demand rather than at boot. Nothing drives the pin until the first
// strike, so start-up -- including joining the network -- happens with the
// servo completely inert.
bool servo_attach() {
  if (s_attached) return true;
  if (s_pin < 0) return false;

  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = (ledc_timer_bit_t)kServoBits;
  t.timer_num = kBellTimer;
  t.freq_hz = SERVO_HZ;
  t.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&t) != ESP_OK) {
    log_e("bell: servo timer rejected; bell disabled");
    return false;
  }

  ledc_channel_config_t c = {};
  c.gpio_num = s_pin;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = kBellChannel;
  c.timer_sel = kBellTimer;
  c.duty = 0;
  c.hpoint = 0;
  if (ledc_channel_config(&c) != ESP_OK) {
    log_e("bell: servo pin %d rejected; bell disabled", s_pin);
    return false;
  }
  s_attached = true;
  return true;
}

void servo_stop() { servo_release(); }

// Celebration sequence. A zero frequency means silence for that step.
struct Chirp { uint16_t hz; uint16_t ms; };
// Two patterns. Built per step rather than stored, so a tone changed on the web
// page takes effect on the very next buzz with no reboot.
bool s_long = false;

uint8_t chirp_count() { return s_long ? 1 : 3; }

Chirp chirp_at(uint8_t i) {
  if (s_long) return {(uint16_t)PIEZO_LONG_HZ, (uint16_t)config::v::piezo_long_ms()};
  switch (i) {
    case 0:  return {(uint16_t)config::v::piezo_hz1(), PIEZO_BEEP1_MS};
    case 1:  return {0, (uint16_t)config::v::piezo_gap_ms()};
    default: return {(uint16_t)config::v::piezo_hz2(), PIEZO_BEEP2_MS};
  }
}

// A long buzz repeating would be alarming rather than informative.
uint8_t repeats() { return s_long ? 1 : (uint8_t)config::v::piezo_repeats(); }

int8_t s_chirp = -1;  // -1 = idle
uint8_t s_repeat = 0;
uint32_t s_chirp_at = 0;

void piezo_silence() {
  // Duty 0 rather than ledc_stop(): both park the pin low, but stop() disables
  // the channel and the next chirp would have to re-enable it. Leaving the
  // piezo driven or floating is what loads the LDO and cooks the board, so
  // every path out of the sequence ends here.
  ledc_set_duty(LEDC_LOW_SPEED_MODE, kPiezoChannel, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kPiezoChannel);
}

}  // namespace

void begin() {
  // The onboard piezo is used only for the short celebration chirp; the rest of
  // the time it must sit low. Waveshare's own FAQ: a passive buzzer behaves
  // like a resistor and draws current continuously if its pin floats or idles
  // high, loading the LDO and making the board run hot. Driving it briefly is
  // fine — that is what it is for — so long as every path ends back at low.
  //
  // Only safe on the new board revision. On the old one the buzzer sits on
  // GPIO33, inside the range the octal PSRAM consumes, so it must not be
  // touched at all — see docs/HARDWARE.md.
#if BOARD_REV_NEW
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  ledc_timer_config_t pt = {};
  pt.speed_mode = LEDC_LOW_SPEED_MODE;
  pt.duty_resolution = LEDC_TIMER_8_BIT;
  pt.timer_num = kPiezoTimer;
  pt.freq_hz = PIEZO_BEEP1_HZ;
  pt.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&pt) != ESP_OK) {
    log_e("bell: piezo timer rejected; buzzer disabled");
    return;
  }

  ledc_channel_config_t pc = {};
  pc.gpio_num = PIN_BUZZER;
  pc.speed_mode = LEDC_LOW_SPEED_MODE;
  pc.channel = kPiezoChannel;
  pc.timer_sel = kPiezoTimer;
  pc.duty = 0;
  pc.hpoint = 0;
  if (ledc_channel_config(&pc) != ESP_OK) {
    log_e("bell: piezo channel rejected; buzzer disabled");
    return;
  }
  piezo_silence();
#endif

  s_pin = config::v::bell_pin();
  s_switch_pin = config::v::bell_switch_pin();
  s_wired = false;

  if (s_switch_pin >= 0) {
    // Pull-up, so an unconnected sensor reads idle and the bell falls back to
    // its deadline rather than never stopping.
    pinMode(s_switch_pin, INPUT_PULLUP);
  }

  if (s_pin >= 0) {
    // No PWM yet. The servo is attached on the first strike and released
    // afterwards, so it is inert through boot and through joining the network.
    s_wired = true;
    servo_release();
  }
}

// The striker's GPIO is a setting, so it can change without a reflash. Detaching
// the old pin first matters: leaving a coil driver attached to a pin we no
// longer manage is how you get a permanently energised solenoid.
void reconfigure() {
  if (config::v::bell_pin() == s_pin &&
      config::v::bell_switch_pin() == s_switch_pin) {
    return;
  }
  if (s_wired) {
    servo_release();
    pinMode(s_pin, INPUT);  // never leave a driver attached to a pin we dropped
  }
  begin();
}

void strike() {
  if (!s_wired) {
    Serial.println("[bell] (no servo GPIO set - pick one on the settings page)");
    return;
  }
  if (!servo_attach()) {
    Serial.println("[bell] servo would not attach");
    return;
  }
  const uint32_t now = millis();
  const bool by_rings = config::v::bell_mode() == 1 && s_switch_pin >= 0;

  s_rings_wanted = by_rings ? config::v::bell_rings() : 0;
  s_rings_seen = 0;
  s_left_idle = false;
  s_last_level = switch_idle() ? 1 : 0;
  s_last_edge_ms = now;

  // The deadline applies in both modes. In timed mode it is the stop; in ring
  // mode it is the backstop for a sensor that never reports.
  s_stop_at = now + (uint32_t)(by_rings ? config::v::bell_max_ms()
                                        : config::v::bell_spin_ms());
  s_running = true;
  servo_write_us(config::v::servo_run_us());
}

void celebrate() {
#if BOARD_REV_NEW
  if (config::v::piezo_repeats() < 1) return;
  s_long = false;
  s_chirp = 0;
  s_repeat = 0;
  s_chirp_at = millis();  // first step runs on the next tick
#endif
}

void long_buzz() {
#if BOARD_REV_NEW
  s_long = true;
  s_chirp = 0;
  s_repeat = 0;
  s_chirp_at = millis();
#endif
}

void play(mood::Cue cue) {
  int choice = 0;
  switch (cue) {
    case mood::Cue::Alert: choice = config::v::alert_sound(); break;
    case mood::Cue::Error: choice = config::v::error_sound(); break;
    case mood::Cue::Done:  choice = config::v::done_sound(); break;
    default: return;
  }
  // 0 none, 1 bell, 2 buzzer, 3 both.
  if (choice & 1) strike();
  if (choice & 2) {
    if (cue == mood::Cue::Done) celebrate();
    else long_buzz();
  }
}

void tick() {
  const uint32_t now = millis();

  if (s_running) {
    if (s_rings_wanted > 0) {
      const int lvl = switch_idle() ? 1 : 0;
      if (lvl != s_last_level && now - s_last_edge_ms >= BELL_DEBOUNCE_MS) {
        s_last_edge_ms = now;
        s_last_level = lvl;
        if (lvl == 0) {
          s_left_idle = true;          // striker on its way through
        } else if (s_left_idle) {
          s_left_idle = false;
          if (++s_rings_seen >= s_rings_wanted) servo_stop();
        }
      }
    }
    // Stopping matters more than starting: a continuous-rotation servo left
    // running does not stop on its own.
    if (s_running && now >= s_stop_at) {
      if (s_rings_wanted > 0 && s_rings_seen < s_rings_wanted) {
        Serial.printf("[bell] gave up after %d of %d rings\n", s_rings_seen,
                      s_rings_wanted);
      }
      servo_stop();
    }
  }

#if BOARD_REV_NEW
  if (s_chirp < 0 || now < s_chirp_at) return;

  if (s_chirp >= chirp_count()) {
    piezo_silence();
    s_repeat++;
    if (s_repeat < repeats()) {
      // Go round again after a gap wide enough to hear as a separate buzz.
      s_chirp = 0;
      s_chirp_at = now + config::v::piezo_repeat_gap_ms();
      return;
    }
    s_chirp = -1;
    return;
  }

  const Chirp c = chirp_at((uint8_t)s_chirp);
  if (c.hz == 0) {
    piezo_silence();
  } else {
    ledc_set_freq(LEDC_LOW_SPEED_MODE, kPiezoTimer, c.hz);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kPiezoChannel, config::v::piezo_duty());
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kPiezoChannel);
  }
  s_chirp_at = now + c.ms;
  s_chirp++;
#endif
}

bool wired() { return s_wired; }

}  // namespace bell
