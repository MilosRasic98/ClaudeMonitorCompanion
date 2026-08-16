# MR26 Claude Notifier

A desk mascot that shows what Claude Code is doing.

A Waveshare ESP32-S3-Touch-LCD-1.69 sits in a 3D-printed shell shaped like the Claude
mascot — the chunky pixel-art crab. The LCD is set into the middle as its face: a flat
orange field with two black bar eyes. The eyes doze when nothing is happening, get bored
when you wander off, focus while Claude works, and snap into a `><` grin the moment Claude
needs you.

Claude Code hooks POST bare event names at the board over the LAN. The board decides how it
feels about them.

- [docs/SPEC.md](docs/SPEC.md) — design, mood engine, open questions
- [docs/HARDWARE.md](docs/HARDWARE.md) — pin map, measured numbers, traps

## Setup

You flash once. After that everything is configured from the board's own web page — Wi-Fi
included — so there is never a second reason to open the firmware.

### 1. Credentials

```bash
cp include/secrets.h.example include/secrets.h
```

Fill in `WIFI_SSID` and `WIFI_PASS`, and set `NOTIFY_TOKEN` to any random string. These are
only the **first-boot defaults**: they are copied into NVS on first run and everything after
that is edited from the settings page. `include/secrets.h` is gitignored.

If you get them wrong, nothing is lost. After four failed joins the board gives up on your
network and hosts its own open access point called `<hostname>-setup`, serving the same
settings page at `http://192.168.4.1/`. Join it from a phone and fix the Wi-Fi there.

### 2. Build and flash

PlatformIO Core lives at `~/.platformio/penv/bin/pio` on this machine and is not on `PATH`.

```bash
~/.platformio/penv/bin/pio run -t upload
```

### 3. Find the board

```bash
~/.platformio/penv/bin/pio device monitor
```

It prints its IP on connect. **Give it a DHCP reservation and use the IP in the hook
config** — on both platforms.

The board does advertise `claude-notifier.local` over mDNS and it resolves fine from a
browser or an interactive `curl`. It is still the wrong choice for hooks: resolution was
measured at **5.1 s per lookup on macOS, uncached**, against the hook's 2 s timeout, so every
hook fails silently. The same request to the raw IP takes **86 ms**. Windows cannot generally
resolve `.local` at all without Bonjour.

Both ends must also be on the **same access point**. Traffic that crosses a range extender's
wireless backhaul was measured at 100% packet loss here; moving the Mac onto the same AP as
the board took it to 0% loss and 27 ms.

### 4. Wire up the hooks

```bash
python3 tools/gen_hooks.py 192.168.1.42 your-token
```

Paste the output into the `hooks` key of `~/.claude/settings.json`. User scope, not project
scope — this is a personal desk peripheral, not something to commit into every repo.

Check it works:

```bash
curl -s -X POST -H "Authorization: Bearer your-token" http://192.168.1.42/e/needs_input
```

The face should snap to `><`. Tap the screen to acknowledge.

## Why the hook config looks like that

Two details in the generated config are load-bearing.

**`args` is present, which selects exec form.** Claude Code spawns `curl` directly with no
shell. Without `args` it is shell form, which goes through `sh` on macOS but **PowerShell**
on Windows when Git Bash is absent — and PowerShell aliases `curl` to `Invoke-WebRequest`,
which takes entirely different flags. Exec form is byte-identical on both platforms, and
`curl` ships with macOS and with every Windows version Claude Code supports.

**`"async": true`.** Hook handlers block Claude Code's agentic loop by default, with a 600 s
timeout. A board that is switched off could otherwise stall a session for ten minutes.
`curl -m 2` bounds the network call as well, so dead-board processes cannot pile up.

Together these mean the host needs no script, no runtime, and no compiled helper. That is
only possible because the event rides in the URL path with no request body — which in turn
is only possible because the board owns the mood state machine and the host reports nothing
but raw facts.

## The settings page

Open the board's IP in a browser. Everything is on one page, grouped:

| Group | What you can change |
|---|---|
| **Sound** | piezo on/off, how many buzzes, both tones, volume, both gaps, the bell's GPIO, whether it rings on alerts and on errors |
| **Face** | style, background shade, eye width / height / gap / vertical position, pixel size, blink timings |
| **Timing** | bored-after, asleep-after, celebration length, sulk length, alert timeout, pokes before annoyed, poke window |
| **Screen** | the four backlight levels |
| **Wi-Fi** | network, password, hostname, hook token |

Plus test buttons for the buzzer, the bell and every mood, and a two-step factory reset.

Changes apply immediately — drag the eye width and the face changes as you watch. Only
Wi-Fi settings restart the board.

**The whole thing is table-driven.** One row in [src/app/config.cpp](src/app/config.cpp)
declares a setting, and its storage, validation, JSON and web form all follow from it. The
page renders whatever the board reports, so adding a setting never means editing HTML. Even
the mood test buttons come from a list the firmware publishes.

Some details that matter:

- **Passwords are never sent to the browser.** A blank password field means "unchanged", so
  saving other settings can never wipe your Wi-Fi key.
- **Out-of-range values are rejected, not clamped silently**, and the page reports the
  failure rather than looking like it worked.
- **Editing is not clobbered by the live refresh.** The page polls every few seconds to show
  the current mood and to pick up changes made on the device itself — swiping the style, for
  instance — but it skips any control you are focused on or have unsaved changes in.
- **No authentication.** It is LAN-only, and the bearer token still protects the `/e/*`
  routes a hook uses. Requiring a password to change a buzzer tone would defeat the point.

## Serial commands

The board takes single keystrokes over USB serial, so the animation can be tuned without a
host in the loop.

| | |
|---|---|
| `p` `f` `n` `e` `i` `s` `x` `a` | fire an event: prompt, turn finished, needs input, error, idle, session start/end, ack |
| `1`–`8` | force a mood: chill, working, excited, confused, angry, bored, sleeping, waking |
| `d` | auto-cycle every mood, 5 s each |
| `[` `]` | cycle the face style — same as swiping |
| `c` | cycle the orange candidates |
| `b` | test the bell |
| `w` | scan for 2.4 GHz networks — marks your SSID with `*` and shows its auth mode |
| `h` | health line |
| `?` | help |

Forcing moods matters because `BORED` is five minutes away and `SLEEPING` is thirty.

## Screens

**Swipe up or down** to move between three screens. **Swipe sideways** to change the face's
style, as before. If your panel reports its axes the other way round, flip
`Swap swipe axes` on the settings page rather than rebuilding.

| Screen | What it shows |
|---|---|
| **Face** | the mascot. The resting screen, and what the shell is built around |
| **Gauge** | a ring showing how much of the five-hour limit is used, with time to reset below |
| **Stats** | IP, signal, uptime, mood, energy, prompts this window, current time |

### Where the gauge's numbers come from

The ring shows the real percentage of your five-hour limit, and the line beneath it the real
time to reset. Both come from Claude Code's **status line**, which is the only place it
exposes them:

```
rate_limits.five_hour.used_percentage    0-100
rate_limits.five_hour.resets_at          unix epoch seconds
```

**No hook carries this.** The only limit-related hook is `StopFailure` with
`error: rate_limit`, and that fires once you have already hit the wall. So
[tools/statusline.py](tools/statusline.py) reads the status line JSON, forwards those two
numbers to `POST /limits`, and prints an ordinary status line. `gen_hooks.py` emits its
config alongside the hooks.

Two details in that script are load-bearing. The POST is **spawned detached and never waited
on**, because Claude Code cancels an in-flight status line script when a new update arrives —
a blocking call would be killed part-way, and would stall the status bar whenever the board
was off. And the status line **prints regardless** of whether forwarding worked: a desk
ornament must never be able to break the editor it decorates.

Without the status line configured, the gauge falls back to timing the window from the
prompts the board has seen and labels itself `EST - NO HOST`. That fallback is only ever a
lower bound — it cannot know about work done before the board was switched on, so it reads
the window as opening later than it really did. This is why the fallback is labelled rather
than dressed up as a quota.

Wall-clock time comes from NTP through the POSIX timezone string in settings. Until the first
sync the gauge reads `NO CLOCK` rather than guessing.

**`statusLine` is read when a session starts.** Hooks hot-reload when you edit settings, the
status line does not — after adding it, restart Claude Code or it will simply never run. The
gauge shows `EST - NO HOST` until the first report arrives, and `device.limit_age_s` on
`/api/config` is -1 until then, which is the quickest way to tell "not running" from
"nothing has changed".

### How often it refreshes

The percentage comes from the host, so it updates whenever the status line runs: on every
assistant message, and at least once a minute via `refreshInterval`. Claude Code debounces
those at 300 ms.

The countdown does not depend on the host at all. `resets_at` is an absolute epoch, so once
the board has it, NTP time is enough to tick the remaining time down every minute — even
with Claude Code closed.

### Drawing a smooth ring cheaply

The ring is a real circle, not a ring of blocks. The two costs are worth separating: testing
every pixel in the bounding box is a few thousand float operations and costs nothing, while
one SPI transaction per pixel would be ruinous. So each row is scanned, runs of identical
colour are coalesced, and one rectangle is pushed per run — a few hundred transactions for
the whole ring, and only when the value changes.

The unfilled part of the track is the background colour darkened at runtime, so it reads
correctly on the orange field and on the red alert field without a second hardcoded colour.

Everything else — the percentage, the countdown, the face — stays on the pixel grid. The
ring is the one place a curve beats blocks, because a dial has to be readable at a glance
from across a desk.

## Faces

The printed mascot exists in several faces, so the firmware does too. **Swipe across the
screen** to cycle them, or `[` and `]` over serial. The choice is saved to NVS and survives
a reboot.

Swipes are derived from the raw coordinate stream, not from the CST816's own gesture
register. The chip only decides a gesture happened once the finger lifts, and it latches the
code afterwards — so a second swipe the same way reads as unchanged and gets dropped, which
is exactly the motion you make flicking through styles. Watching coordinates instead means
the swipe fires the moment it crosses the threshold, mid-drag. Thresholds are `SWIPE_MIN_PX`
and friends in `tuning.h`; sampling runs at 5 ms.

| Style | On screen |
|---|---|
| `plain` | two bar eyes, nothing else — the default character |
| `grin` | `><` chevrons, always, regardless of mood |
| `round` | round spectacles with a bridge, pupils animating inside them, barred grill mouth |
| `pixel` | chunky rectangular glasses with white glints, open smile |
| `shades` | deal-with-it sunglasses and a handlebar moustache |

Style is a costume; mood is the expression underneath. Both apply at once — a bored mascot
in shades is still bored, it just shows it differently.

That last part is why the opaque styles work at all. When the lenses hide the eyes, the mood
has nowhere to go, so the glasses inherit the motion instead: they bounce when excited and
droop when bored. The eyes are pinned still in those styles precisely so the accessories can
carry it.

Spectacles are the other interesting case. The eyes shrink to pupils and their travel is
penned inside the lenses, but a blink still scales proportionally, so it reads as a blink at
pupil size rather than a clipped version of the full-size one.

Accessories are static art, drawn once when the style changes and again only when they move.
Curves — the lens rings, the smile, the moustache — are drawn one grid row at a time, so a
circle comes out as a staircase. That is the point.

## Which hooks drive what

| Hook | Matcher | Board event | Face |
|---|---|---|---|
| `UserPromptSubmit` | — | `prompt_submitted` | `WORKING` |
| `PermissionRequest` | — | `needs_input` | **red, sticky** |
| `PreToolUse` | `AskUserQuestion\|ExitPlanMode` | `needs_input` | **red, sticky** |
| `PostToolUse` | `AskUserQuestion\|ExitPlanMode` | `answered` | back to `WORKING` |
| `Notification` | `permission_prompt` | `needs_input` | **red, sticky** |
| `Notification` | `idle_prompt` | `idle` | `CHILL` |
| `Stop` | — | `turn_finished` | `EXCITED`, 6 s |
| `PostToolUseFailure` / `StopFailure` | `*` / — | `error` | `CONFUSED` |
| `SessionStart` / `SessionEnd` | — | session lifecycle | — |

**`PermissionRequest` is the one that matters** for "Claude is waiting on me". The docs are
explicit that it is narrower than `PreToolUse`: it fires *only* when Claude Code is about to
show a permission dialog, not before every tool call. So it costs nothing when nothing is
being asked. `Notification`/`permission_prompt` covers the same ground but ~6 s later; it is
kept because re-asserting the same state is harmless and it catches anything the first one
misses.

`PreToolUse` matches on tool name, so scoping it to `AskUserQuestion` and `ExitPlanMode` —
the two tools whose entire purpose is asking the user something — means no other tool call
ever spawns a process.

`needs_input` deliberately does **not** clear the running-turn flag: a permission prompt
happens *during* a turn, so answering it returns the face to `WORKING`, not `CHILL`. The
paired `PostToolUse` hook is what clears it. A sticky alert also self-expires after
`ALERT_MAX_MS` as a safety net, so a missed clear can never strand the face red.

## Mood model

Two inputs: events pushed from the host, and time the board tracks itself.

| Mood | Enters when | Eyes |
|---|---|---|
| `SLEEPING` | nothing for ~30 min | closed to flat bars, slow breathing drift |
| `WAKING` | first event after sleeping | bars grow with a slight overshoot |
| `CHILL` | recent activity, nothing running | full bars, random blinks, slow drift |
| `BORED` | no new prompt for 5 min | 60% height, drooped and looking away, long blinks |
| `WORKING` | between prompt and turn finished | full height, sweeping left-right scan |
| `EXCITED` | needs input, or turn finished | `><` chevrons, bounce, bell |
| `CONFUSED` | a turn failed | one eye squinting, slow shake |
| `ANGRY` | poked five times in four seconds | one fixed set of slanted brows, red field |

**The field turns red whenever the mascot wants something** — an unacknowledged `needs
input`, or a fit of pique. Visible across a room, and it does the bell's job until the bell
exists. A finished turn does *not* go red: that is a celebration, not a demand.

**Poke it and it shakes the poke off.** Every tap runs a fast head-shake over whatever mood
is playing. Keep poking — five taps inside four seconds — and it snaps to `ANGRY`: one fixed
set of slanted brows, no animation, red background, seven seconds to cool off. Boredom and
sleep are suppressed while it sulks.

`BORED` and `ASLEEP` cannot come from hooks — `idle_prompt` fires once and never repeats,
and nothing fires at all when Claude Code is not running. They are elapsed-time decay on the
board, which is also why the face keeps behaving when Wi-Fi drops.

Alerts are sticky. `needs_input` waits to be acknowledged; boredom and sleep are suppressed
until it is, so something you still have to deal with never quietly turns into a yawn. A
finished turn is a six-second celebration that decays on its own.

**Energy** is a separate 0–100 value that decays continuously and is bumped by every event.
It modulates blink rate, sweep speed and bounce amplitude *within* a mood, so a quiet
afternoon and a heavy refactor look different even when both are nominally `WORKING`.

## Tuning the face

Everything lives in [include/tuning.h](include/tuning.h): palette, grid quantum, eye
geometry, per-mood motion amplitudes, mood thresholds, energy rates, backlight levels. One
edit and a reflash. Nothing is hardcoded elsewhere.

`GRID_Q` is the interesting one. Every coordinate is snapped to it, which is what makes the
motion step like pixel art instead of gliding. It also sets how many distinct steps a blink
has — about 20 at the current 5 px, which reads as deliberate. At 10 px it would be 10,
which reads as broken.

## Performance

Measured on the actual board, 240x280 at 80 MHz SPI:

| | |
|---|---|
| Frame rate | 62 fps, held during a Wi-Fi association retry storm |
| Frame cost, `CHILL` / `BORED` / `CONFUSED` | 78–160 µs |
| Frame cost, `EXCITED`, bare eyes | 703 µs |
| Frame cost, `EXCITED`, accessory styles | 0.6–2.0 ms |
| Full-screen fill | 18.6 ms |
| Flash / RAM | 12.5% / 14.6% |

Under 5% of the 16 ms frame budget at worst. Three things get it there: only the pixels that
actually changed are pushed, rendering runs in its own task on core 1 while Wi-Fi stays on
core 0, and every solid colour has its own pre-filled DMA buffer so transfers pipeline
without ever waiting.

## Dependencies

None. No LVGL, no ESPAsyncWebServer, no JSON library, no vendor SDK — just the Arduino core
and the IDF components it already ships.

## Sound

Two separate noises, deliberately.

**The striker rings on anything that wants you.** Every event that turns the field red — a
permission prompt, a question, being poked past patience — plus errors. Not on finished
turns; those are not demands.

**The onboard piezo buzzes when a turn finishes** — the "something is done" signal. A rising
pair, 2.7 kHz then 3.5 kHz, and the whole pattern repeats once so it lands as *buzz buzz*
rather than a single flourish. Press `z` to hear it on demand.

`PIEZO_REPEATS` in `tuning.h` sets how many times; `PIEZO_REPEAT_GAP_MS` is deliberately
wider than the gap inside the pattern, so it reads as two buzzes rather than four beeps.

That second one looks like it contradicts the "hold the piezo low" rule, and does not. The
hazard is the pin *idling* high or floating, which makes a passive buzzer draw current
continuously and cook the LDO. Driving it briefly is what it is for. Every path out of the
chirp sequence — including the gap between the two beeps and the end — goes through
`piezo_silence()`, which parks the duty at zero. It uses `ledc_set_duty(0)` rather than
`ledc_stop()` because stop disables the channel and the next chirp would have to re-enable
it.

Three LEDC timers, one each for backlight, striker and piezo. Channels sharing a timer
corrupt each other's frequency, and the piezo changes frequency mid-sequence.

## Wi-Fi troubleshooting

The board supervises its own reconnection: `setAutoReconnect()` stops trying after enough
failures, which leaves the board offline forever with nothing in the log. Every
`WIFI_RETRY_MS` it tears the association down and starts again, logging the attempt.

Press `w` for a scan. It disconnects first — scanning mid-handshake returns an empty list
that looks like "no networks" and is not — then lists what it can see, marks your SSID with
`*`, and prints each network's auth mode. Read the disconnect reason alongside it:

| Reason | Means |
|---|---|
| `201 NO_AP_FOUND` | SSID not visible. Usually a 5 GHz-only network — the ESP32-S3 has no 5 GHz radio |
| `202 AUTH_FAIL` | AP rejected the key. Wrong password |
| `15 4WAY_HANDSHAKE_TIMEOUT` | Handshake started and stalled. Wrong password, or WPA3/PMF required |

If the password contains a backslash or a double quote it must be escaped in `secrets.h` —
it is a C string literal, so `\` and `\"`.

## Verified on hardware

Everything below was measured on the real board, not inferred.

| | |
|---|---|
| HTTP endpoints | 12/12 — every event route, plus 401 on missing/bad token, 404 on unknown event and on the wrong verb |
| Link reliability | 20/20 single-shot, once both ends share an access point |
| Hook argv | 8/8 fire, 43–166 ms each, using the exact `command` + `args` Claude Code will execute |
| Face | all eight moods, all five styles, red alert field, angry face, poke shake |
| Render | 62 fps sustained, 78 µs–2 ms per frame, held through a Wi-Fi retry storm |

## Not done yet

- The mechanical bell. An external striker on LEDC PWM; the actuator, drive stage and GPIO
  are undecided, so `PIN_BELL` is `-1` and `bell::strike()` logs instead. Everything that
  should ring it is already wired — see Sound below.
- Eye geometry is placeholder until the shell's screen cutout is measured.
- Which of the four orange candidates matches the filament.
- Touch coordinate orientation is unverified against a real finger. Swipe cycling keys off
  whichever axis dominates, so a horizontal flick works even if the driver's axes turn out
  swapped; only the direction could need flipping, via `SWIPE_INVERT`.
- Hats are not rendered. The top hat and the sailor hat in the reference photos sit above
  the head, outside the screen cutout — they belong to the printed shell, not the firmware.
