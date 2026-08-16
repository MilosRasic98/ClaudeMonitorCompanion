// CST816D/T capacitive touch driver, Waveshare ESP32-S3-Touch-LCD-1.69.
//
// The chip is on the shared I2C bus set up by the caller (Wire.begin() has
// already run) at 7-bit address 0x15. Everything here is synchronous: no
// ISR, no task, no queue. poll() is meant to be called once per render frame
// and either hands back a fresh Event or returns false immediately.
//
// Register map. Hynitron's own CST816D datasheet (V1.3, the one linked from
// Waveshare's product wiki) documents the electrical/timing spec and the I2C
// address (0x15, i.e. write 0x2A / read 0x2B) but does NOT publish the touch
// data register table -- that part is missing from the public datasheet.
// The addresses below are cross-checked across four independent open-source
// drivers that all agree byte-for-byte:
//   - fbiego/CST816S            (Arduino library, MIT, PlatformIO registry)
//   - espressif/esp-bsp         (Espressif's own esp_lcd_touch_cst816s BSP)
//   - esphome/esphome           (cst816 touchscreen component)
//   - PupaLupa44/CST816T_STM32  (bare-metal STM32 HAL driver)
// All four give the same 0x01/0x02/0x03-0x06/0xA7/0xFA/0xFE layout used
// below. The one register the task sheet asked to verify that did NOT
// cross-check is 0xA5 "power mode" -- only one of the four (fbiego) writes
// it, to enter standby (value 0x03), and the other three never touch it.
// We don't need standby (board is always USB-powered), so it is left alone
// rather than guessed at.
#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace touch {

namespace {

constexpr uint8_t kAddr = 0x15;

// Touch data block. GestureID(0x01)/FingerNum(0x02)/XposH(0x03)/XposL(0x04)/
// YposH(0x05)/YposL(0x06) are contiguous, so one 6-byte read starting at
// kRegGestureId pulls the whole block into buf[0..5] in that order.
constexpr uint8_t kRegGestureId    = 0x01;
constexpr uint8_t kRegChipId       = 0xA7;
constexpr uint8_t kRegIrqCtl       = 0xFA;
constexpr uint8_t kRegDisAutoSleep = 0xFE;

// IrqCtl bits (same four sources above, plus corroborated independently by
// a LilyGO T-Display-S3 community register dump). We want INT to pulse on
// touch-down, on release (state change), and on a completed gesture -- that
// covers every case poll() cares about.
constexpr uint8_t kIrqEnTouch  = 0x40;
constexpr uint8_t kIrqEnChange = 0x20;
constexpr uint8_t kIrqEnMotion = 0x10;

// Fallback poll period. INT is the fast path; this bounds how long a missed
// edge (or an IRQ line that never got wired/configured right) can wedge the
// driver into never reading the chip again. Kept short because swipes are
// tracked from the coordinate stream, and 50 ms between samples was enough to
// make a fast flick look like two points.
constexpr uint32_t kFallbackPollMs = 12;

bool s_ready = false;
uint8_t s_chip_id = 0;
bool s_last_pressed = false;
int16_t s_last_x = 0;
int16_t s_last_y = 0;
uint8_t s_last_gesture_raw = 0;
uint32_t s_last_check_ms = 0;

bool read_regs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  // Repeated start: keep the bus so the read targets this register instead
  // of wherever the chip's internal pointer happened to be.
  if (Wire.endTransmission(false) != 0) return false;

  const size_t got = Wire.requestFrom((int)kAddr, (int)len);
  if (got != len) return false;

  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool write_reg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(kAddr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

Gesture map_gesture(uint8_t raw) {
  switch (raw) {
    case 0x00: return Gesture::None;
    case 0x01: return Gesture::SwipeUp;
    case 0x02: return Gesture::SwipeDown;
    case 0x03: return Gesture::SwipeLeft;
    case 0x04: return Gesture::SwipeRight;
    case 0x05: return Gesture::Tap;
    case 0x0B: return Gesture::DoubleTap;
    case 0x0C: return Gesture::LongPress;
    default:   return Gesture::None;  // unrecognised code -- don't guess
  }
}

}  // namespace

bool begin() {
  pinMode(PIN_TP_RST, OUTPUT);
  // IRQ is a chip-driven push-pull output per the datasheet's pin table (the
  // "open-drain/pull-up optional" note is on SCL/SDA only, not IRQ), so no
  // internal pull is needed here.
  pinMode(PIN_TP_INT, INPUT);

  // Reset pulse. Datasheet section 7.1 specs the RST low pulse (Trst) at
  // >= 0.1 ms and the post-reset reinit time (Tron) at >= 100 ms; several of
  // the open-source drivers above get away with ~50 ms, but this only runs
  // once at boot, so there is no reason not to give it the documented
  // margin.
  digitalWrite(PIN_TP_RST, HIGH);
  delay(5);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(100);

  uint8_t id = 0;
  if (!read_regs(kRegChipId, &id, 1)) return false;
  // 0x00 and 0xFF are both "nothing answered" patterns (all-clear or
  // all-pullup bus), not real chip IDs -- CST816S/D/T all report values in
  // the 0xB4-0xB7 range.
  if (id == 0x00 || id == 0xFF) return false;
  s_chip_id = id;

  // Always USB-powered, so the chip's ~2 s auto-standby timer is pure
  // downside: a sleeping touch chip needs another reset pulse to wake,
  // which poll() has no business doing mid-frame. Disable it.
  write_reg(kRegDisAutoSleep, 0xFE);

  write_reg(kRegIrqCtl, kIrqEnTouch | kIrqEnChange | kIrqEnMotion);

  s_last_pressed = false;
  s_last_x = 0;
  s_last_y = 0;
  s_last_gesture_raw = 0;
  s_last_check_ms = millis();
  s_ready = true;
  return true;
}

bool poll(Event &out) {
  if (!s_ready) return false;

  const uint32_t now = millis();

  // IRQ is active-low: the chip pulls it down to signal fresh data.
  bool should_check = digitalRead(PIN_TP_INT) == LOW;

  if (!should_check && (now - s_last_check_ms) >= kFallbackPollMs) {
    should_check = true;
  }
  if (!should_check) return false;

  s_last_check_ms = now;

  uint8_t buf[6];
  if (!read_regs(kRegGestureId, buf, sizeof(buf))) return false;

  const uint8_t gesture_raw = buf[0];
  const uint8_t finger_num = buf[1] & 0x0F;
  const bool pressed = finger_num > 0;

  // High nibble of XposH/YposH carries an event-type/finger-id tag in the
  // upper bits on this chip family; only the low 4 bits are coordinate.
  int16_t x = (int16_t)(((buf[2] & 0x0F) << 8) | buf[3]);
  int16_t y = (int16_t)(((buf[4] & 0x0F) << 8) | buf[5]);

  // NEEDS HARDWARE VERIFICATION: whether raw X maps to panel X and raw Y to
  // panel Y (versus swapped/mirrored) has not been checked against real
  // touches on this unit. No transform is applied here on purpose -- fix it
  // in one place, above, once someone touches a known corner and sees where
  // it lands.
  if (x < 0) x = 0;
  if (x > LCD_W - 1) x = LCD_W - 1;
  if (y < 0) y = 0;
  if (y > LCD_H - 1) y = LCD_H - 1;

  // A gesture code only counts as a new event the first poll it appears on;
  // it commonly stays latched in the register until the next gesture, and
  // re-firing the same one every 50 ms fallback tick would be wrong.
  const bool gesture_changed = gesture_raw != 0 && gesture_raw != s_last_gesture_raw;
  const bool press_changed = pressed != s_last_pressed;
  const bool moved = pressed && (x != s_last_x || y != s_last_y);

  const bool has_event = press_changed || moved || gesture_changed;

  s_last_pressed = pressed;
  s_last_x = x;
  s_last_y = y;
  s_last_gesture_raw = gesture_raw;

  if (!has_event) return false;

  out.pressed = pressed;
  out.x = x;
  out.y = y;
  out.gesture = gesture_changed ? map_gesture(gesture_raw) : Gesture::None;
  return true;
}

uint8_t chip_id() { return s_chip_id; }

}  // namespace touch
