#!/usr/bin/env python3
"""Claude Code status line that also feeds the mascot its rate-limit figures.

Usage, as the `statusLine.command` in ~/.claude/settings.json:

    python3 /path/to/tools/statusline.py <board-host> <token>

Why this exists: the board wants to show how much of the five-hour limit is
gone and when it resets, and *hooks cannot tell it*. No hook payload carries
that. The status line's JSON does:

    rate_limits.five_hour.used_percentage    0-100
    rate_limits.five_hour.resets_at          unix epoch seconds

So this reads that, forwards it, and prints an ordinary status line.

Two details are load-bearing:

  Fire and forget. Claude Code cancels an in-flight status line script when a
  new update arrives, so a blocking HTTP call would be killed part-way and, on
  a slow or absent board, would stall the status bar every time it ran. The
  POST is spawned detached and never waited on.

  Printing happens regardless. If the board is off, unreachable, or the
  arguments are wrong, the status line still renders. A desk ornament must
  never be able to break the editor it is decorating.
"""

import json
import os
import subprocess
import sys


def forward(host, token, rl):
    """Spawn a detached POST. Never blocks, never raises into the caller."""
    pct = rl.get("used_percentage")
    if pct is None:
        return
    resets = int(rl.get("resets_at") or 0)
    url = f"http://{host}/limits?pct={int(round(float(pct)))}&reset={resets}"
    try:
        subprocess.Popen(
            ["curl", "-s", "-m", "2", "-X", "POST",
             "-H", f"Authorization: Bearer {token}", url],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
    except Exception:
        pass


def bar(pct, width=10):
    filled = int(round(pct / 100 * width))
    return "[" + "#" * filled + "-" * (width - filled) + "]"


def main():
    try:
        data = json.load(sys.stdin)
    except Exception:
        print("claude")
        return 0

    host = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("MASCOT_HOST")
    token = sys.argv[2] if len(sys.argv) > 2 else os.environ.get("MASCOT_TOKEN", "")

    five = (data.get("rate_limits") or {}).get("five_hour") or {}
    if host and five:
        forward(host, token, five)

    model = (data.get("model") or {}).get("display_name", "claude")
    ctx = (data.get("context_window") or {}).get("used_percentage")
    cwd = os.path.basename((data.get("workspace") or {}).get("current_dir", "") or "")

    parts = [model]
    if cwd:
        parts.append(cwd)
    if ctx is not None:
        parts.append(f"ctx {int(float(ctx))}%")

    pct = five.get("used_percentage")
    if pct is not None:
        parts.append(f"5h {bar(float(pct))} {int(round(float(pct)))}%")

    print(" | ".join(parts))
    return 0


if __name__ == "__main__":
    sys.exit(main())
