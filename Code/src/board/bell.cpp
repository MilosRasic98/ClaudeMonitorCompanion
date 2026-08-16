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
int s_pin = -1;  // resolved from config at begin()/reconfigure()
uint32_t s_release_at = 0;

// Celebration sequence. A zero frequency means silence for that step.
struct Chirp { uint16_t hz; uint16_t ms; };
const uint8_t kChirpCount = 3;

// Built per step rather than stored, so changing a tone on the web page takes
// effect on the very next buzz with no reboot.
Chirp chirp_at(uint8_t i) {
  switch (i) {
    case 0:  return {(uint16_t)config::v::piezo_hz1(), PIEZO_BEEP1_MS};
    case 1:  return {0, (uint16_t)config::v::piezo_gap_ms()};
    default: return {(uint16_t)config::v::piezo_hz2(), PIEZO_BEEP2_MS};
  }
}

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
  ESP_ERROR_CHECK(ledc_timer_config(&pt));

  ledc_channel_config_t pc = {};
  pc.gpio_num = PIN_BUZZER;
  pc.speed_mode = LEDC_LOW_SPEED_MODE;
  pc.channel = kPiezoChannel;
  pc.timer_sel = kPiezoTimer;
  pc.duty = 0;
  pc.hpoint = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&pc));
  piezo_silence();
#endif

  s_pin = config::v::bell_pin();
  s_wired = false;
  if (s_pin >= 0) {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_8_BIT;
  t.timer_num = kBellTimer;
  t.freq_hz = BELL_PWM_HZ;
  t.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&t));

  ledc_channel_config_t c = {};
  c.gpio_num = s_pin;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = kBellChannel;
  c.timer_sel = kBellTimer;
  c.duty = 0;
  c.hpoint = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&c));
  s_wired = true;
  }
}

// The striker's GPIO is a setting, so it can change without a reflash. Detaching
// the old pin first matters: leaving a coil driver attached to a pin we no
// longer manage is how you get a permanently energised solenoid.
void reconfigure() {
  if (config::v::bell_pin() == s_pin) return;
  if (s_wired) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, kBellChannel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, kBellChannel);
    ledc_stop(LEDC_LOW_SPEED_MODE, kBellChannel, 0);
    pinMode(s_pin, INPUT);
  }
  begin();
}

void strike() {
  if (!s_wired) {
    Serial.println("[bell] (no GPIO set — pick one on the settings page)");
    return;
  }
  ledc_set_duty(LEDC_LOW_SPEED_MODE, kBellChannel, BELL_STRIKE_DUTY);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kBellChannel);
  s_release_at = millis() + BELL_STRIKE_MS;
}

void celebrate() {
#if BOARD_REV_NEW
  if (!config::v::piezo_enabled() || config::v::piezo_repeats() < 1) return;
  s_chirp = 0;
  s_repeat = 0;
  s_chirp_at = millis();  // first step runs on the next tick
#endif
}

void tick() {
  const uint32_t now = millis();

  if (s_release_at && now >= s_release_at) {
    s_release_at = 0;
    if (s_wired) {
      // Releasing matters more than striking: a coil left energised is a heater.
      ledc_set_duty(LEDC_LOW_SPEED_MODE, kBellChannel, 0);
      ledc_update_duty(LEDC_LOW_SPEED_MODE, kBellChannel);
    }
  }

#if BOARD_REV_NEW
  if (s_chirp < 0 || now < s_chirp_at) return;

  if (s_chirp >= kChirpCount) {
    piezo_silence();
    s_repeat++;
    if (s_repeat < config::v::piezo_repeats()) {
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
