#pragma once
#include <stdint.h>

// ST7789V2 panel access. Every shape this project draws is a solid rectangle,
// so that is the entire drawing primitive.

namespace display {

// Brings up SPI, the panel, and the backlight. Fatal on failure.
bool begin();

// Solid rectangle in panel coordinates. Colour is pre-swapped RGB565 — use the
// COLOR() macro from tuning.h, never a raw literal.
void fill_rect(int x, int y, int w, int h, uint16_t color);

// 0..255. The backlight has its own LEDC timer: channels sharing a timer can
// corrupt each other's frequency, and the bell will want PWM too.
void backlight(uint8_t duty);

}  // namespace display
