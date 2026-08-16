# Claude Monitor Companion

A desk mascot that shows what Claude Code is doing.

A Waveshare ESP32-S3-Touch-LCD-1.69 sits in a 3D-printed shell shaped like the Claude
mascot — the chunky pixel-art crab. The LCD is set into the middle as its face: a flat
orange field with two black bar eyes. The eyes doze when nothing is happening, get bored
when you wander off, focus while Claude works, and snap into a `><` grin the moment Claude
needs you. Hit the five-hour usage limit and the whole screen goes red with a blinking
exclamation mark.

Claude Code hooks POST bare event names at the board over your LAN. The board decides how it
feels about them.

## Repository layout

| | |
|---|---|
| [`Code/`](Code) | Firmware. PlatformIO + Arduino, ESP32-S3. Start at [Code/README.md](Code/README.md) |

CAD, photographs and build notes will land alongside `Code/` as the project comes together.

## What it does

**Seven moods**, driven partly by events from your computer and partly by time the board
tracks itself:

| Mood | When |
|---|---|
| `SLEEPING` | nothing for ~30 minutes |
| `CHILL` | recent activity, nothing running |
| `BORED` | no new prompt for ~5 minutes |
| `WORKING` | between your prompt and Claude finishing |
| `EXCITED` | Claude needs you, or a turn just finished |
| `CONFUSED` | something failed |
| `ANGRY` | you poked it five times in four seconds |
| `LIMIT` | usage limit reached — red field, blinking `!` |

**Three screens.** Swipe up or down for the mascot, a usage-window gauge showing time left
and when the limit resets, or a plain stats screen. Swipe sideways to change the face.

**Five faces**, taken from the printed mascot's variants: bare eyes, a `><` grin, round
spectacles with a grill mouth, chunky pixel glasses with a smile, and deal-with-it shades
with a moustache. Swipe the screen to cycle them.

**Sound.** An optional external mechanical striker rings for anything that wants your
attention. The onboard piezo buzzes twice when a turn finishes.

**A settings page**, served by the board itself. Thirty-four settings — sound, face
geometry, mood thresholds, brightness, Wi-Fi — all editable from a phone. Flash once and you
never open the firmware again. Get the Wi-Fi wrong and the board hosts its own access point
so you can fix it.

## Hardware

- [Waveshare ESP32-S3-Touch-LCD-1.69](https://www.waveshare.com/esp32-s3-touch-lcd-1.69.htm)
  — ESP32-S3R8, 8 MB PSRAM, 16 MB flash, 240x280 ST7789V2, CST816 touch
- USB-C power, always on. No battery.
- Optional: a mechanical bell striker on a spare GPIO
- A 3D-printed shell with a cutout for the screen

Full pin map, measured performance figures and the hardware traps worth knowing are in
[Code/docs/HARDWARE.md](Code/docs/HARDWARE.md).

## Quick start

```bash
cd Code
cp include/secrets.h.example include/secrets.h   # first-boot Wi-Fi defaults
pio run -t upload
pio device monitor                               # prints the board's IP
```

Then open that IP in a browser to configure everything else, and wire up the Claude Code
hooks:

```bash
python3 tools/gen_hooks.py <board-ip> <your-token>
```

Paste the output into the `hooks` key of `~/.claude/settings.json`. The full walkthrough,
including why the hook config is shaped the way it is, is in [Code/README.md](Code/README.md).

## Notes on the build

No external libraries. No LVGL, no async web server, no JSON library — just the Arduino core
and the ESP-IDF components it already ships. The face is drawn as quantised rectangles
straight onto the panel, which is both the correct pixel-art look and fast: 62 fps at under
5% of the frame budget.

## Licence

GPL-3.0. See [LICENSE](LICENSE).
