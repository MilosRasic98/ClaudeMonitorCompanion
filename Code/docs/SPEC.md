# MR26 Claude Notifier — draft spec

Status: **built and running on hardware.** Everything below is implemented except the bell,
which is blocked on hardware decisions. Items marked ❓ are still open. Setup and usage:
[README.md](../README.md).

Two things were learned on hardware that are worth carrying forward:

- `esp_lcd_panel_draw_bitmap()` is asynchronous. Rewriting a shared strip buffer between
  calls corrupts the transfer still in flight, which showed up as eyes visibly tearing apart
  during sideways movement. Fixed by giving each solid colour its own pre-filled DMA buffer
  so nothing is ever mutated mid-flight. That also removed the need to block per transfer:
  worst-case frame cost fell from 2.9 ms to 0.7 ms.
- The core-separation bet paid off. With the board stuck in a Wi-Fi association retry storm
  on core 0, the render task on core 1 held 62 fps throughout.

## What it is

A small mascot head that sits on top of a monitor. The 240x280 screen is its **face** —
two animated eyes and a few accents. The face mirrors what Claude Code is doing on the
computer: dozing when nothing is happening, bored when you have wandered off, focused
while Claude works, and visibly thrilled the moment it needs you.

It is an ambient-presence object first and a notifier second. The animation quality is
the product. A dashboard that happened to have eyes would be the wrong build.

## The mood engine

Two inputs drive the face:

1. **Events** pushed from the computer when something happens in Claude Code.
2. **Time**, which the board tracks itself.

That split matters: "bored" and "asleep" are the *absence* of events, so the board has to
own the state machine. The host only reports facts; the board decides how it feels about
them. This also means the face keeps behaving correctly if the host goes quiet, sleeps, or
the Wi-Fi drops.

### Moods

| Mood | Enters when | Face |
|---|---|---|
| `SLEEPING` | no event for ~30 min ❓ | eyes closed to a curved line, slow breathing bob, occasional drifting Z |
| `WAKING` | first event after sleeping | lids crack open, one stretch, settles into `CHILL` |
| `CHILL` | recent activity, nothing running | eyes open, random blinks every 2–6 s, slow saccades, micro-drift |
| `BORED` | awake, no new user prompt for 5 min ❓ | lids droop, pupils drift off to one corner, periodic yawn, long slow blinks |
| `WORKING` | Claude is actively running | pupils scan in a repeating sweep, slight concentration squint, faster blink |
| `EXCITED` | Claude needs the human — waiting on input, or turn finished | eyes wide, pupils dilate, vertical bounce with overshoot, sparkle accent, one bell strike |
| `CONFUSED` | a turn failed | one eye squints, brow angles in, slow horizontal head-shake |

### Energy

A `0..100` scalar, separate from mood. Each event bumps it; it decays continuously toward
zero. This is the "how hard is Claude being used right now" signal, and it modulates the
face *within* a mood rather than switching moods:

- blink rate and saccade frequency
- animation playback speed
- pupil size (bigger when energised)
- how emphatic the `EXCITED` bounce is

So a quiet afternoon of one-off questions and a heavy refactor session look different even
when both are nominally `WORKING`.

### Touch

Tap the face and it reacts — a happy blink, pupils snap to the touch point. Tapping while
`EXCITED` also acknowledges the alert and silences the bell. Petting the thing should feel
like it does something. ❓ Should tap-to-ack be the only way to clear an alert, or should
it also clear on the next event from the host?

## Physical form

A 3D-printed shell in the outline of the Claude mascot — the little pixel-art crab — with
the LCD set into the middle as its face. The screen therefore shows **only** a flat orange
field with black eyes on it. The body, claws, and silhouette are plastic, not pixels.

Consequences for the firmware:

- The LCD's rounded corners get hidden behind the shell's cutout, so the full rectangle is
  usable and nothing has to dodge the corners.
- ❓ The cutout's shape and size are unknown. Eyes should be laid out around the screen
  centre with a generous safe margin until it is measured.
- ❓ The on-screen orange has to relate to the filament colour — either matched so the face
  reads as one surface, or deliberately brighter so it glows against the shell. Needs a
  physical sample to judge; the value lives in `tuning.h` either way.

## Face construction

Reference: photos of the printed mascot. Established from them —

- The body is flat, saturated orange. The eyes are flat black. Two colours, no gradients,
  no outlines, no shading.
- The **default face is two tall narrow black rectangles**, roughly 1:3 width to height,
  separated by about two eye-widths, sitting slightly above centre.
- A canonical alternate face exists: **`>` `<` chevron eyes**, inward-pointing, chunky and
  stair-stepped. That is the happy/squint variant.
- No mouth in the default form. The versions with smiles, mustaches, tophats and sunglasses
  are physical accessories clipped onto the print, not part of the base character.

So the confirmed on-screen job is: **an orange field with two black rectangles that change
shape.** That is a much smaller problem than the animated-eyeball design this spec started
with, and it settles the renderer.

### Parametric shapes, rasterised to a quantised grid

An eye is a rectangle with four animated parameters: height, width, vertical offset, and
horizontal offset. A blink is height going to near-zero and back. Droop is height down plus
offset down. Everything that interpolates is expressed this way — no sprite sheet needed for
the common case.

The 8-bit look comes from **snapping every coordinate to a grid quantum** — proposed **5 px**
— before drawing. Motion then steps in visible increments exactly like the printed character.

**Quantum choice matters more than it sounds.** It sets how many distinct steps a blink has:
an eye about 100 px tall gives ~20 steps at a 5 px quantum, ~10 at 10 px. Ten reads as janky,
twenty reads as deliberate. Starting at 5 px, and it is one constant.

### Getting pixels to the panel

Naive approaches both lose, for reasons that are specific to this face:

- **One SPI transaction per changed cell** is catastrophic. A 10 x 10 cell is 200 bytes —
  20 µs of payload against roughly 45 µs of CASET + RASET + RAMWR overhead, so 69% of the
  time is command overhead. Pushing every cell individually costs more than blitting the
  entire screen.
- **A single coalesced bounding box** fails on this specific layout. The two eyes sit at
  opposite sides of the screen, so their union spans nearly the full width and the box is
  most of the frame.

What works: rasterise the parametric shapes into a small logical grid, diff it against the
previous frame, and **for each changed cell-row emit its contiguous changed spans** — two per
row for two eyes, skipping the gap between them. If a row somehow needs more than two spans,
coalesce it to one full-width strip rather than spraying transactions.

A 30 px wide span over a 5 px row is about 75 µs all-in, so a blink touching ~20 rows costs
roughly 3 ms against a 16 ms frame budget. Full screen at 80 MHz is 13.4 ms, so this leaves
4x headroom and the budget goes on animation quality instead of throughput.

Grid at a 5 px quantum is 48 x 56 cells = 2,688 bytes, times two for the previous-frame copy.
Plus a double-buffered strip. Everything stays in internal SRAM; PSRAM never enters the
render path.

### Driver and threading

- **esp_lcd directly**, with `trans_queue_depth = 10` and the `on_color_trans_done` callback.
  Available under Arduino-ESP32 3.x by including the IDF headers; use `SPI2_HOST` and never
  call `SPI.begin()`. The callback fires in ISR context — notify a task, do no work there.
  Arduino_GFX's `Arduino_ESP32SPIDMA` backend is an acceptable fallback for ergonomics; plain
  `Arduino_ESP32SPI` is not, because it busy-waits with no DMA.
- **Render task pinned to core 1**, priority 3, 4 kB stack, paced with `vTaskDelayUntil` on a
  16 ms period so jitter cannot accumulate. Not serviced from `loop()`.
- **All Wi-Fi and HTTP on core 0**, which is where Arduino-ESP32 pins the Wi-Fi task anyway.
  The Wi-Fi driver runs at priority 23 and lwIP at 18 — far above any sane render priority —
  and can hold a core for milliseconds during association or DHCP. On a notifier that is
  maximally perverse: frames would drop exactly when a notification arrives, which is the one
  moment the animation has to look good.
- **Hot blit path in `IRAM_ATTR`**, DMA buffers in internal SRAM. Any flash write disables the
  cache on both cores and would otherwise stall the render loop.
- No LVGL tick to manage. `esp_timer_get_time()` drives the keyframe timeline directly.

**Tearing.** The panel exposes no TE pin — confirmed from the schematic — so drawing cannot be
synced to the scan. This turns out not to matter: dirty-span pushes are 1–4 ms, so the tear
window is small, and chunky quantised motion hides tearing far better than smooth motion
would. The pixel-art direction helped here.

### Sprite frames only where geometry runs out

Chevron `><` eyes, the sleeping closed-eye curve, Z's and the sparkle are not rectangles.
Those get small hand-drawn PNG frames converted by `tools/sprite_gen.py` into 1-bit masks —
1-bit, not RGB565, because there are only ever two colours. A 60 x 60 mask is 450 bytes.

Hybrid, deliberately: parametric for anything that interpolates (blinks, droop, drift,
bounce), sprites for fixed shapes. Pure sprites would make smooth blinking need dozens of
frames; pure geometry could not draw a chevron.

### LVGL — dropped

Decision taken, and the numbers back it. Waveshare's own LVGL config on this board allocates
two draw buffers of 33,600 bytes each plus a 48 kB heap — about 115 kB of RAM — against
roughly 11 kB for the renderer above. Flash is 100–150 kB against single-digit kB. And the
obvious mapping of a cell grid onto `lv_obj`s would be ~120 kB in objects alone, with a tree
walk and per-object style and mask work every refresh, to do what is fundamentally a
`memset`.

The subtler argument is the decisive one: **LVGL's animation model is the wrong one here.**
`lv_anim` provides easing curves over continuous values, but a quantised grid throws away
sub-pixel precision. Pixel art wants a keyframe timeline — a list of `{shape, hold_ms}` —
which LVGL does not provide. We would be fighting it, then writing the ~50-line keyframe
player anyway.

The one genuine loss is `lv_indev`'s input plumbing, and it is small: the CST816 does gesture
detection in hardware and raises an interrupt.

Rendering goes directly on esp_lcd, with CST816 touch read directly. Revisit only if
something later needs real text on screen — and if it does, LVGL 8.4 rather than 9.x, which
benchmarks slower on the ESP32-S3.

### Layout

```
ui/face/
  eye.cpp        one eye: height, width, offsets, quantise + draw
  masks.cpp      1-bit sprite blitter for chevrons, Z's, sparkle
  moods.cpp      per-mood animation programs built from the above
  frames.h       generated by tools/sprite_gen.py — do not hand-edit
  tuning.h       every timing constant and the palette, in one place
```

`tuning.h` matters more than it looks. Getting a face to feel alive is iterative — blink
duration, the pause before a saccade, the overshoot on the bounce. Those need to be one
edit and a reflash away, not buried across five files.

### Palette calibration

The on-screen orange and the filament orange will not match by hex — one is backlit and
emissive, the other diffuse and lit by the room. Matching them numerically is the wrong
approach.

So: ship a **calibration mode** (long-press on boot, say) that cycles a handful of candidate
oranges live while the board sits in the printed shell. Pick by eye, save to NVS. Ten
minutes of work that saves an argument with a colour picker.

### Mood → face, restated for real geometry

| Mood | Eyes |
|---|---|
| `SLEEPING` | closed — a short flat bar each, slow vertical breathing drift, occasional Z sprite |
| `WAKING` | bars grow from flat to full height, slight overshoot |
| `CHILL` | full-height bars, random blinks every 2–6 s, occasional lateral drift of both bars together |
| `BORED` | bars at ~60% height, shifted down and both to one side, long slow blinks, periodic droop-and-recover |
| `WORKING` | full height, bars sweep left–right in a repeating scan, faster blink rate |
| `EXCITED` | snap to the `><` chevron sprite, vertical bounce with overshoot, sparkle, bell strike |
| `CONFUSED` | one bar short and one full, small tilt, slow side-to-side shake |

### Accessories — later, optional

The printed variants suggest a cheap trick: sunglasses dropping down over the eyes as a
transient reaction. Pure sprite work, zero architectural cost, easy to add once the base
face is alive. Not v1.

## Architecture

```
Computer (macOS now, Windows later)          ESP32-S3-Touch-LCD-1.69
┌────────────────────────────────┐          ┌─────────────────────────────┐
│ Claude Code hooks              │  HTTP    │ web server :80              │
│  → curl, exec form, async      │ ───────► │  POST /e/{type}             │
│    no shell, no script         │  LAN     │  GET  /health               │
└────────────────────────────────┘          │         │                   │
                                            │         ▼                   │
                                            │  mood engine + energy       │
                                            │         │                   │
                                            │         ▼                   │
                                            │  pixel-grid face renderer   │
                                            └─────────────────────────────┘
```

The board is the stateful half. The host is a dumb event emitter — which is exactly what
makes the Windows port cheap.

### Board API

| Route | Effect |
|---|---|
| `POST /e/{type}` | feeds the mood engine; bumps energy |
| `GET /health` | `{"uptime":…,"rssi":…,"mood":"…","energy":…}` |

Event types are raw facts, not moods: `prompt_submitted`, `turn_finished`, `needs_input`,
`idle`, `error`, `session_started`, `session_ended`. Unknown types get a 404 rather than
being silently swallowed, so a typo in the hook config is visible.

Shared bearer token as a static `-H` argument on `POST`; the board should not take orders
from anything on the LAN that finds port 80. Discovery: see the host section.

## Cross-platform host — settled, and simpler than expected

Requirement: one setup that works on macOS and Windows. Mac first, Windows not a later
bolt-on.

### No script, no binary, no shell

Claude Code hook commands have two execution forms, selected by whether an `args` array is
present:

- **Shell form** (`command` string only) goes through a shell — `sh -c` on macOS, Git Bash
  on Windows if installed, **PowerShell if not**. PowerShell aliases `curl` to
  `Invoke-WebRequest`, which takes completely different flags. A hook written as plain
  `curl -X POST -d '...'` is a coin flip on a Windows box.
- **Exec form** (`command` + `args`) spawns the executable directly. No shell on any
  platform. No tokenization, no quoting rules, no alias.

Exec form is therefore byte-for-byte identical across macOS and Windows, and `curl` is
guaranteed present on both: macOS ships `/usr/bin/curl`, and Windows has shipped
`curl.exe` since 1803 while Claude Code requires 1809 or newer.

The other half of the trick is that **our host never needs to send dynamic data.** The
board owns the mood engine, so a hook only has to report *which* event happened — and that
is known at config time, one hook registration per event. No stdin parsing, no JSON
assembly, so no interpreter and no shipped binary.

The event therefore rides in the URL path rather than a body, which removes the last place
quoting could matter:

```
POST http://<board>/e/prompt_submitted
POST http://<board>/e/turn_finished
POST http://<board>/e/needs_input
POST http://<board>/e/error
```

A compiled Go/Rust helper stays the upgrade path if we later want session names or message
text on screen. Given the face has no text on it, that may never happen.

### Hook mapping

| Claude Code hook | matcher | Board event |
|---|---|---|
| `UserPromptSubmit` | — | `prompt_submitted` |
| `Stop` | — | `turn_finished` |
| `Notification` | `permission_prompt` | `needs_input` |
| `Notification` | `idle_prompt` | `idle` |
| `PostToolUseFailure` | `*` | `error` |
| `StopFailure` | — | `error` |
| `SessionStart` | — | `session_started` |
| `SessionEnd` | — | `session_ended` |

`WORKING` is the span between `prompt_submitted` and `turn_finished` — inferred board-side,
with a safety timeout so a session that dies mid-turn does not leave the face working
forever.

Deliberately **not** hooking `PreToolUse` / `PostToolUse`. They would give a nice
fine-grained energy signal, but they fire on every single tool call and each one spawns a
process. Energy comes from prompt and turn rate instead. If the face ends up feeling flat
during long turns, adding `PostToolUse` alone is a one-line change. ❓

`BORED` and `ASLEEP` are confirmed impossible to source from hooks — `idle_prompt` fires
once, ~60 s after a stop, and never repeats, and nothing fires at all when Claude Code is
not running. Board-side decay timers, as designed. From the board's side, "idle" and "not
running" are the same thing: silence.

### Never stall the human

Hook handlers **block Claude Code's agentic loop by default**, with a 600 s default timeout.
An unreachable board on a blocking hook could hang a session for ten minutes.

Two independent guards, both required:

- `"async": true` on every hook entry — documented fire-and-forget, output and exit code
  ignored.
- `curl -m 2` — bounds the network call itself so dead-board processes do not pile up.

`type: "http"` hooks, where Claude Code does the POST itself with no command at all, look
tempting and are **disqualified**: they have no `async` field, so a dead board can stall
the loop. A peripheral that is often switched off must never be on a blocking path.

### Finding the board

mDNS `.local` works natively on macOS. On Windows it is **not** reliable — Windows 10/11
have partial mDNS wired into modern device-discovery APIs, but not into the Win32 resolver
that `curl` and `ping` use, so `board.local` typically fails without Apple's Bonjour
installed.

So: the board advertises mDNS for the Mac path, but the documented setup is a **DHCP
reservation and a literal IP in the hook config**. It is one line the user edits once, and
it works identically on both platforms.

### Where the config lives

`~/.claude/settings.json`, user scope — this is a personal desk peripheral, not something
to commit into every project. That file is **outside this project directory**; nothing
writes to it without explicit go-ahead. Deliverable is a generated snippet in `tools/` plus
paste instructions.

## Firmware layout

```
platformio.ini
src/
  main.cpp            boot, task wiring
  board/pins.h        pin map from docs/HARDWARE.md
  board/display.cpp   ST7789V2 init, 20-row offset, cell blit
  board/touch.cpp     CST816 over I2C, raw tap coordinates
  board/backlight.cpp LEDC PWM, dim when chill, full when excited
  board/bell.cpp      external striker on LEDC; holds onboard piezo low
  net/wifi.cpp        STA from secrets.h, reconnect, mDNS
  net/httpd.cpp       the two routes, JSON parse, token check
  app/mood.cpp        state machine, energy decay, timers
  ui/face/…           as above
include/secrets.h     gitignored; SSID, password, bearer token
```

## Power — settled

Always USB. No Li-Po, no sleep modes, no battery gauge, no `SYS_EN` latch. Backlight PWM
is for expression (dim while dozing, full when excited), not for saving power. Battery ADC
on GPIO1 goes unused.

## Audible alert — mechanical bell

Not the onboard piezo. An external mechanical striker driven from a GPIO through LEDC PWM.
Still to be worked out with the user: actuator type, drive current, whether a MOSFET stage
and flyback diode are needed, which free GPIO it lands on, and what PWM frequency and duty
actually move it rather than just heating the coil.

Independent of that: the **onboard passive buzzer pin gets driven low at boot** even though
we never use it. Unused and floating is exactly the case that loads the LDO and cooks the
board.

## Build

PlatformIO + Arduino (Arduino-ESP32 3.x), `esp32-s3-devkitc-1` with 16 MB flash and octal
PSRAM set explicitly. No LVGL. Display driven through the IDF `esp_lcd` component, which
Arduino-ESP32 3.x compiles in. SPI at 80 MHz, with 40 MHz as the documented fallback.

**Zero external libraries so far.** The path-based event API carries no body, so there is no
JSON to parse; the handlers are three lines each, so the Arduino core's synchronous
`WebServer` does the job and `ESPAsyncWebServer` is not needed; and the CST816 driver is
written directly against `Wire`. `lewisxhe/SensorLib@0.2.1` only if the IMU or RTC get used.

A blocking HTTP handler is safe here specifically because rendering lives in its own
higher-priority task on the other core. Verified empirically: with a Wi-Fi association
retry storm running on core 0, the render task held 62 fps at 46 µs/frame.

PlatformIO Core 6.1.19 is installed at `~/.platformio/penv/bin/pio` — not on `PATH`, call
it by full path. The `espressif32` platform and xtensa-esp32s3 toolchain were already
cached.

## Open questions

1. **Screen cutout geometry.** The single biggest unknown left. Eye size, spacing and
   vertical placement are all relative to how much of the 240 x 280 panel the shell
   actually exposes, and whether the visible area is square or a portrait slot. Everything
   in `tuning.h` is guesswork until this is measured.
2. **Bored / sleep thresholds** — 5 min and 30 min are placeholders.
5. **Tap-to-ack semantics** — see Touch above.
6. **Board revision** — unknown; changes RTC_INT / SYS_EN / SYS_OUT / onboard buzzer pins.
   Needs a look at the silkscreen.
7. **Bell hardware** — deferred by agreement. Blocks `board/bell.cpp` only.
8. **RTC** — the PCF85063 is there. Any use, or ignore it? A clock does not obviously fit
   a face.
9. **Energy resolution** — prompt/turn rate only, or also hook `PostToolUse` for a
   finer-grained signal at the cost of a process per tool call. Start without; revisit if
   the face reads flat during long turns.

## Verified externally

- The whole host-side design (exec form, `async`, hook names and matchers, the
  `BORED`/`ASLEEP` gap, Windows mDNS) comes from the Claude Code hooks and settings docs
  as of August 2026. Worth a re-read before implementing, since hooks are an actively
  moving part of the product.
- Not yet verified empirically: PowerShell's `curl` alias behaviour under a shell-form
  hook. We avoid the situation entirely by using exec form, so this stays academic unless
  someone hand-writes a shell-form hook.
