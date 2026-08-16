# MR26 — Claude Notifier

A mascot head on a Waveshare ESP32-S3-Touch-LCD-1.69 (1.69" 240x280 touch LCD, ESP32-S3R8,
8 MB PSRAM, 16 MB flash) that sits on top of a monitor. The screen is its face: two
animated eyes whose mood mirrors what Claude Code is doing on the computer — dozing,
bored, focused, or thrilled that it needs you.

**The animation quality is the product.** A dashboard with eyes on it would be the wrong
build. When a tradeoff comes up between feature count and how alive the face feels, the
face wins.

- Board reference and pin map: [docs/HARDWARE.md](docs/HARDWARE.md)
- Mood engine, API, open questions: [docs/SPEC.md](docs/SPEC.md)

## Design invariants

- **The board owns the mood state machine.** The host only reports raw events
  (`prompt_submitted`, `needs_input`, …). "Bored" and "asleep" are the absence of events,
  so they can only be tracked board-side — and it keeps the face alive when the host goes
  quiet or Wi-Fi drops.
- **The host stays dumb and thin.** The setup must work on macOS *and* Windows. Mac first,
  but Windows is not a later bolt-on. Every host-side decision gets checked against
  "does this still work under cmd.exe".
- **All face timing lives in `ui/face/tuning.h`.** Blink duration, saccade pauses, bounce
  overshoot. Tuning a face is iterative; those constants must be one edit from a reflash,
  never scattered.

## Build

PlatformIO Core 6.1.19 at `~/.platformio/penv/bin/pio` — not on `PATH`, call by full path.

## Hardware rules that bite

- Drive the onboard buzzer pin low early in `setup()`, even though we do not use it. A
  floating/idle-high passive buzzer draws current continuously, loads the LDO, and heats
  the board. The audible alert is an external mechanical striker on LEDC PWM instead.
- The panel is 240x280 inside a 240x320 controller: apply the 20-row offset.
- Always USB-powered. No battery, no sleep modes, no `SYS_EN` power latch needed.
- Onboard buzzer, RTC_INT, SYS_EN, and SYS_OUT moved between board revisions. Confirm the
  revision before using those pins.
