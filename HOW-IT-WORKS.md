# How it works

The short version: **Claude Code runs `curl`.** That's the whole integration.

No daemon, no library, no cloud service, no API key on the device, and for
almost everything, no script either.

---

## The one line that does most of the work

Claude Code has a feature called **hooks**: entries in its settings file that run
a command when something happens. The command here is literally `curl`:

```json
{
  "type": "command",
  "command": "curl",
  "args": [
    "-s", "-m", "2", "-X", "POST",
    "-H", "Authorization: Bearer <token>",
    "http://192.168.1.26/e/turn_finished"
  ],
  "async": true
}
```

That is the entire mechanism. Claude finishes a turn, Claude Code fires that
`curl`, the board gets a POST, the face grins.

There are twelve of these, one per thing worth knowing about. They differ only
in the last word of the URL.

## Why there is no JSON, and why that matters

The event name is in the **URL path**, not in a request body.

That sounds like a detail; it is the reason no script is needed. With no body,
there is nothing to build, nothing to quote, and nothing to escape — so the
command is a fixed list of arguments that never changes. `curl` ships with
macOS, with Windows, and with essentially every Linux, and the same config works
byte-for-byte on all three.

Two smaller decisions carry real weight:

**`args` as a list, not a string.** Claude Code spawns `curl` directly instead of
handing a string to a shell. No quoting rules, no PATH surprises, and no
difference between platforms. (A string would go through `sh` on macOS but
PowerShell on Windows, where `curl` is an alias for something else entirely.)

**`async: true`.** The board is a desk ornament. It must never be able to slow
down the editor. Hooks normally block Claude Code until they finish; this one is
fire-and-forget, and `-m 2` caps the request at two seconds. Unplug the board
mid-session and nothing notices.

## What the computer sends, and what the board decides

The computer reports **facts**. It never says how to feel about them.

| Claude Code does this | The board is told |
|---|---|
| you submit a prompt | `prompt_submitted` |
| a turn finishes | `turn_finished` |
| a permission prompt appears | `needs_input` |
| Claude asks you a question | `needs_input` |
| you answer it | `answered` |
| a tool call fails | `error` |
| the usage limit is hit | `limit_reached` |
| a session starts or ends | `session_started` / `session_ended` |

Everything expressive happens **on the board**: which mood those facts add up
to, how the eyes move, when to go red, when to ring.

That split is not tidiness, it is necessary. Two of the moods — **bored** and
**asleep** — are the *absence* of events. No hook can fire to say "nothing has
happened for five minutes". Only something keeping its own clock can know that,
so the board keeps the clock. A useful side effect: the face keeps behaving
correctly when the computer sleeps, the Wi-Fi drops, or Claude Code is closed.

## The one exception: the usage dials

The two ring gauges — five-hour and weekly — are the only part that needs a
script, for one reason: **no hook carries your rate limits.**

They exist in exactly one place, the JSON Claude Code feeds to its **status
line**. So a small script sits there, reads two numbers out, and forwards them
to the board with the same `curl` as everything else. It prints an ordinary
status line at the same time, so it earns its keep either way.

Everything else — moods, faces, colours, sounds, the bell — works with the
script absent.

## What the board never does

- **It never talks to Anthropic.** No API key, no account, no token beyond the
  shared word it checks on incoming requests.
- **It never leaves your network.** The only outbound traffic is an NTP time
  sync at boot, so it can say "resets at 15:33" instead of counting down.
- **It has no app.** The settings page is served by the board itself. You open
  its IP in a browser and you are talking to the ESP32, not to a service.

If your internet goes down, the mascot carries on.

## The whole picture

```
   your computer                            the board
   ─────────────                            ─────────
   Claude Code
     │
     ├─ hook fires ──► curl ──► HTTP POST ──► /e/needs_input
     │                          (your LAN)         │
     │                                             ▼
     │                                      mood engine + clock
     │                                             │
     └─ status line ─► script ─► POST ────► /limits    │
        (usage % only)                              ▼
                                          face · dials · bell
                                                    │
                                       browser ◄─── settings page
```

Twelve `curl` invocations and one small script. That is the entire protocol.

## Why it was built this way

The goal was that someone could build this without becoming its maintainer.
Flash the board once, open its web page, and never touch the firmware again —
Wi-Fi credentials included. Forty-six settings live on that page, from the
buzzer's pitch to the eye spacing, and there is a pixel editor for drawing your
own faces.

Keeping the wire protocol down to "run `curl`" is what makes the rest of that
possible. There is no version to keep in step, no dependency to break, and
nothing to reinstall when Claude Code updates.
