# Waveshare ESP32-S3-Touch-LCD-1.69 — hardware reference

Product page: <https://www.waveshare.com/esp32-s3-touch-lcd-1.69.htm>
Wiki: <https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69>
Official examples: <https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.69>
Schematic (PDF): <https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69/ESP32-S3-Touch-LCD-1.69-Sch.pdf>

## Core

| Item | Value |
|---|---|
| MCU | ESP32-S3R8, Xtensa LX7 dual-core, up to 240 MHz |
| SRAM / ROM | 512 KB / 384 KB |
| PSRAM | 8 MB (octal, on-package) |
| Flash | 16 MB external |
| Radio | Wi-Fi 2.4 GHz 802.11 b/g/n, Bluetooth 5 (BLE); onboard patch antenna |
| USB | Type-C wired to the ESP32-S3 native USB (USB-CDC for flashing + logs) |

## Peripherals

| Block | Part | Bus |
|---|---|---|
| Display | ST7789V2, 1.69", 240 (H) x 280 (V), 262K colour, RGB565 in the stock demos | 4-wire SPI |
| Touch | CST816D/T capacitive, 7-bit I2C address `0x15` (write `0x2A`, read `0x2B`), 10 kHz–400 kHz | I2C |
| IMU | QMI8658C, 3-axis accel + 3-axis gyro | I2C |
| RTC | PCF85063 + SH1.0 backup-battery header | I2C |
| Charger | ETA6098 Li-ion charger, MX1.25 battery connector | — |
| Audio | passive buzzer | GPIO/PWM |
| Buttons | BOOT, RST, and a multi-function PWR button (single / double / long press) | — |

The ST7789V2 controller has 240 x 320 of RAM but the panel is 240 x 280, so the
display window needs the usual 20-pixel row offset. Corners are rounded — keep
anything important away from them.

## Pin map

Cross-checked two ways: `examples/arduino/libraries/Mylibrary/pin_config.h` in the official
repo, and the net labels extracted from the schematic PDF. They agree.

| Signal | GPIO | Confirmed by |
|---|---|---|
| LCD_DC | 4 | both |
| LCD_CS | 5 | both |
| LCD_SCK / LCD_SCL | 6 | both |
| LCD_MOSI / LCD_SDA | 7 | both |
| LCD_RST | 8 | both |
| LCD_BL (backlight) | 15 | both |
| I2C SCL | 10 | both |
| I2C SDA | 11 | both |
| TP_RST | 13 | both |
| TP_INT | 14 | both |
| Battery voltage ADC | 1 | wiki only |

**One shared I2C bus, confirmed.** The schematic ties `TP_SCL` and `RTC_SCL` both to GPIO10,
and `TP_SDA` / `RTC_SDA` both to GPIO11. Touch, RTC, and IMU are all on it.

**No TE pin.** The LCD FPC connector carries only `LCD_DC`, `LCD_CS`, `LCD_SCL`, `LCD_SDA`,
`LCD_RST`, `LEDK`, the supplies and the touch signals. There is no tearing-effect output, so
frame-synced drawing is not possible — see Rendering constraints below.

**An SHTC3 temperature/humidity part appears in the schematic** on the same I2C bus. Likely
unpopulated on this SKU (the schematic is shared across board variants). An I2C scan will
settle it.

### Pins that moved between board revisions — and why

The wiki publishes an old-vs-new table:

| Signal | Old revision | New revision |
|---|---|---|
| Buzzer | GPIO33 | GPIO42 |
| RTC_INT | GPIO41 | GPIO39 |
| SYS_EN (power latch) | GPIO35 | GPIO41 |
| SYS_OUT (PWR button sense) | GPIO36 | GPIO40 |

**The published schematic is the old revision** — its net labels read `GPIO35 → SYS_EN` and
`GPIO36 → SYS_OUT`.

The reason for the change looks clear, and it matters. **GPIO33–37 are consumed by the
in-package octal PSRAM on the ESP32-S3R8** (SPIIO4–7 and SPIDQS). The old revision put the
buzzer on 33, SYS_EN on 35, and SYS_OUT on 36 — all inside that reserved range, so on an
8 MB-PSRAM part they cannot work as general IO. The new revision moves all of them to
39–42, which are clear.

Practical consequence: **do not design anything onto GPIO 33–37.** If this unit turns out to
be an old revision, its buzzer and power-button pins are unusable rather than merely
different, and the PWR button is off the table.

### Free GPIOs for the mechanical bell

Taken: 4, 5, 6, 7, 8, 15 (LCD), 10, 11, 13, 14 (I2C + touch), 1 (battery ADC),
19/20 (USB), 33–37 (octal PSRAM), 39–42 (buzzer / RTC_INT / SYS_EN / SYS_OUT on the new
revision), plus the flash SPI pins and GPIO0 (BOOT).

Reasonable candidates, avoiding strapping pins (0, 3, 45, 46) and the UART0 pair (43, 44):
**17, 18, 21, 38, 47, 48**. Needs a multimeter check against the actual board before
committing.

## Rendering constraints

Established from the ST7789 datasheet, ESP-IDF documentation, and the Waveshare examples.

| Item | Value |
|---|---|
| ST7789 serial write cycle, `TSCYCW` | 16 ns min → **62.5 MHz** spec ceiling for writes |
| ST7789 serial read cycle, `TSCYCR` | 150 ns min → 6.67 MHz for reads |
| ESP32-S3 SPI clocks actually selectable | 80, 40, 26.67, 20 MHz — integer divisors of 80 MHz, nothing between |
| Waveshare's own examples | pin 40 MHz in both the Arduino and IDF versions |
| Full frame, 240 x 280 RGB565 | 134,400 bytes → 13.4 ms at 80 MHz, 26.9 ms at 40 MHz |
| SPI transaction overhead | 9 µs polling/CPU, 11 µs polling/DMA, 26 µs interrupt/DMA |
| Per-rect command cost | CASET + RASET + RAMWR emitted on every `draw_bitmap`, ~35–50 µs |

80 MHz is a 28% overclock past the datasheet, but it is what the field runs, including a
report covering this exact 240x280 panel. Waveshare's 40 MHz is the conservative choice.
Start at 80, validate at temperature inside the printed shell, fall back to 40 if artifacts
appear — there is no step in between.

### Traps

- **`INVON` is mandatory.** The IDF example calls `esp_lcd_panel_invert_color(panel, true)`.
  Skip it on a hand-rolled init and the orange field comes up cyan.
- **The 20-row offset has two pairs.** Waveshare's Arduino call is
  `Arduino_ST7789(bus, LCD_RST, 0, true, 240, 280, 0, 20, 0, 20)`. The second pair applies
  under rotation 2/3; set only the first and the image shifts when rotated. In esp_lcd it is
  one call: `esp_lcd_panel_set_gap(panel, 0, 20)`.
- **RGB565 byte order.** ST7789 wants big-endian, the ESP32 is little-endian. With only two
  colours on screen, byte-swap the palette once at startup and every fill is correct for
  free.
- **`Arduino_ESP32SPI` does not use DMA.** It pokes the SPI FIFO directly and busy-waits in
  a spin loop, 32 pixels at a time — 2,100 chunked transfers and 100% CPU for a full frame.
  `Arduino_ESP32SPIDMA` is the real DMA backend in the same library.
- **LEDC channels can share a timer and corrupt each other's frequency.** With both the
  backlight and the bell on PWM, give each its own LEDC timer via `ledcAttachChannel`.
- **GPIO 6/7 are not the SPI2 IO_MUX pins** (those are 10–13), so the LCD routes through the
  GPIO matrix. The documented 26/40 MHz GPIO-matrix ceilings are input/MISO setup limits and
  the panel is write-only, so 80 MHz output should be fine — but this is the single claim
  most worth validating on hardware early.
- **Two bugs in Waveshare's examples not to copy:** a stray `esp_timer` in the Arduino demo
  that calls `esp_restart()` after 30 ticks, and `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y` in the
  IDF `sdkconfig.defaults` on a 16 MB board.

### Power-button latch

Pressing PWR connects battery power and boots the board. Firmware must then drive
`SYS_EN` high to hold the latch closed; releasing PWR does not cut power. After boot,
PWR pulls `SYS_OUT` low, so firmware reads `SYS_OUT` to detect single / double / long
press. Driving `SYS_EN` low disconnects battery power — that is the software power-off.

### Buzzer warning

Waveshare's FAQ: drive the buzzer pin low early in boot. The passive buzzer behaves like
a resistor and draws current continuously if the pin floats or idles high, which loads
the LDO and makes the board run hot.

## Toolchains on this machine (checked 2026-08-15)

| Tool | State |
|---|---|
| PlatformIO Core | 6.1.19 at `~/.platformio/penv/bin/pio` — not on `PATH` |
| `espressif32` platform | 7.0.1 (Arduino-ESP32 2.0.17 / IDF 4.4.7) |
| Arduino IDE.app | installed |
| ESP-IDF (`idf.py`) | not installed |
| `arduino-cli` | not installed |
| Node / npm | not installed |
| Python | **3.9.6 system only.** No Homebrew, pyenv, uv or conda |
| Board | attached via a Dell dock: `/dev/cu.usbmodem202201`, Espressif USB JTAG/serial unit, VID `0x303A` PID `0x1001` |

### Why we are on Arduino-ESP32 2.0.17 rather than 3.x

Arduino-ESP32 3.x on PlatformIO means the pioarduino fork, which **hard-requires Python
3.10+**. This machine has only system Python 3.9.6 and no package manager to get a newer
one, so the platform installs and then refuses to build.

The cost is small: esp_lcd exists in IDF 4.4 with the same ST7789 driver, `set_gap`, and
`invert_color`. Two API names differ and are handled behind `ESP_IDF_VERSION` checks in
`src/main.cpp` — `color_space` vs `rgb_ele_order`, and `esp_lcd_panel_disp_off()` vs
`esp_lcd_panel_disp_on_off()`.

To move to 3.x later: install Python 3.10+, rebuild the PlatformIO venv, and swap the
`platform` line in `platformio.ini` (the pioarduino URL is in a comment there).

## Library versions Waveshare pins for the Arduino examples

| Library | Version | Notes |
|---|---|---|
| GFX_Library_for_Arduino | 1.4.9 | ST7789 driver |
| lvgl | 8.4.0 | offline install recommended |
| SensorLib | 0.2.1 | PCF85063 + QMI8658 |
| Arduino_DriveBus | — | CST816 touch, offline install only |
| Mylibrary | — | the `pin_config.h` above, offline install only |
| esp32 board package | >= 3.0.5 | — |

## Measured on this unit (2026-08-15, bring-up firmware)

| Item | Result |
|---|---|
| Chip | ESP32-S3 QFN56, revision v0.2, MAC `a0:f2:62:e4:c5:14` |
| PSRAM | embedded 8 MB (AP_3v3) — octal, so GPIO 33–37 are confirmed reserved |
| Flash | 16 MB, quad, 3.3 V, manufacturer `0x20` device `0x4018` |
| USB | USB-Serial/JTAG, no UART bridge |
| I2C devices on GPIO 10/11 | `0x15` CST816 touch, `0x51` PCF85063 RTC, `0x6B` QMI8658C IMU, `0x7E` unidentified |
| Board revision | **new** — confirmed by the piezo responding on GPIO42 |
| Onboard piezo | audible on GPIO42 via LEDC; two-chirp celebration confirmed by ear |
| SHTC3 | **absent** — nothing at `0x70`, so that footprint is unpopulated on this SKU |
| Full-screen fill, 240x280 | **18.65 ms** |
| Blink frame, both eyes, dirty spans only | **579 µs** |

**80 MHz SPI is confirmed working.** The full-screen fill measures 18.65 ms. Wire time at
80 MHz is 13.4 ms, and the fill runs as 56 chunked transactions, so ~5 ms of command
overhead is expected. At 40 MHz the wire time alone would be 26.9 ms — well above what was
measured — so the clock took.

**Render headroom is enormous.** A blink costs 579 µs against a 16 ms frame budget: under
4%. The dirty-span approach is doing what it was supposed to, and the remaining budget can
go entirely on animation richness.

`0x7E` sits in the 0x78–0x7F range that I2C reserves for 10-bit addressing, which most
scanners skip. Most likely a scan artifact rather than a real device — worth revisiting
only if something on the bus misbehaves.

## Unverified

- I2C addresses for the QMI8658C and PCF85063. An I2C scan on GPIO 10/11 settles this and
  the SHTC3 question in one go.
- ~~Which board revision this unit is.~~ **Resolved: new revision.** Driving GPIO42 with
  LEDC produces audible sound from the onboard piezo, which only holds on the new pin map.
  So RTC_INT is 39, SYS_EN 41, SYS_OUT 40, buzzer 42, and GPIO 33-37 stay off-limits to the
  octal PSRAM.
- Whether 80 MHz SPI is stable on this unit at temperature inside a printed shell.
