# Bill of materials

Everything needed for one mascot, including the optional bell striker. The board
and a USB-C cable are the only mandatory items — the bell, the servo and the
microswitch can all be left out, and the firmware detects their absence.

## Ordered parts

| # | Part | Manufacturer | Qty | Farnell | Newark |
|---|---|---|---|---|---|
| 1 | Continuous rotation servo | Adafruit | 1 | 2816371 | 85W1247 |
| 2 | Microswitch | Multicomp Pro | 1 | 3553972 | 84AH0093 |

## Everything else

| # | Part | Notes |
|---|---|---|
| 1 | [Waveshare ESP32-S3-Touch-LCD-1.69](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.69) | ESP32-S3R8, 8 MB PSRAM, 16 MB flash, 240×280 ST7789V2, CST816 touch |
| 2 | Mechanical desk bell | Any sprung-striker call bell |
| 3 | 3D printing filament | Orange for the shell, black for the bell frame |
| 4 | M2 and M3 screws | Assorted lengths |
| 5 | Perfboard, wire, headers | For the servo and switch connections |
| 6 | 10 kΩ resistor | Pulldown on the microswitch |
| 7 | USB-C breakout ×2 | Power in, power through to the board |

## Wiring

See [Schematics.png](Schematics.png). Two connections carry everything:

| Signal | Pin | Notes |
|---|---|---|
| Servo signal | GPIO 18 | 50 Hz PWM. 2500 µs runs, 1500 µs stops |
| Microswitch | GPIO 17 | COM to 3V3, NO to the pin, 10 kΩ to ground |

Both are configurable from the board's settings page, so a different wiring
choice needs no firmware change. Setting either to `-1` disables that half.

The servo runs from 5 V, not from the board's 3V3 rail. The signal line is the
only thing the ESP32 drives, and the firmware releases the pin when the bell is
idle so a resting servo draws nothing.
