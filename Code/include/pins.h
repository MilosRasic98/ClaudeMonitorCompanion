#pragma once

// Waveshare ESP32-S3-Touch-LCD-1.69 pin map.
// Cross-checked against the vendor's Mylibrary/pin_config.h and the net labels
// in the official schematic PDF. See docs/HARDWARE.md.

// LCD — ST7789V2, 4-wire SPI
#define PIN_LCD_DC    4
#define PIN_LCD_CS    5
#define PIN_LCD_SCK   6
#define PIN_LCD_MOSI  7
#define PIN_LCD_RST   8
#define PIN_LCD_BL    15

// Panel geometry. The controller has 240x320 of RAM behind a 240x280 panel,
// hence the row offset.
#define LCD_W         240
#define LCD_H         280
#define LCD_GAP_X     0
#define LCD_GAP_Y     20

// One shared I2C bus: CST816 touch, QMI8658C IMU, PCF85063 RTC,
// and possibly an SHTC3 (may be unpopulated on this SKU).
#define PIN_I2C_SCL   10
#define PIN_I2C_SDA   11
#define PIN_TP_RST    13
#define PIN_TP_INT    14

// Battery divider. Unused — this build is always USB-powered.
#define PIN_BAT_ADC   1

// Revision-dependent pins. GPIO 33-37 are consumed by the in-package octal
// PSRAM on the ESP32-S3R8, so the old revision's assignments (buzzer 33,
// SYS_EN 35, SYS_OUT 36) are not merely different — they cannot work.
//
// CONFIRMED NEW REVISION: driving GPIO42 with LEDC produces audible sound from
// the onboard piezo on this unit, which is only true of the new map. The
// old-revision branch below is kept for anyone building against a different
// board, but is not the one in use here.
#define BOARD_REV_NEW 1
#if BOARD_REV_NEW
  #define PIN_BUZZER   42
  #define PIN_RTC_INT  39
  #define PIN_SYS_EN   41
  #define PIN_SYS_OUT  40
#else
  #define PIN_BUZZER   33  // unusable with octal PSRAM
  #define PIN_RTC_INT  41
  #define PIN_SYS_EN   35  // unusable with octal PSRAM
  #define PIN_SYS_OUT  36  // unusable with octal PSRAM
#endif

// External mechanical bell striker, on LEDC PWM. Set to a real GPIO once the
// actuator and drive stage are chosen; -1 keeps the bell code inert.
// Candidates, avoiding strapping pins (0, 3, 45, 46) and UART0 (43, 44):
// 17, 18, 21, 38, 47, 48.
#define PIN_BELL (-1)
