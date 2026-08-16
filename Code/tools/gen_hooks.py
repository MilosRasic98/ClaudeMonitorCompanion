#!/usr/bin/env python3
"""Print the Claude Code hook config for the mascot.

Usage:
    python3 tools/gen_hooks.py <host> <token>
    python3 tools/gen_hooks.py 192.168.1.42 my-token

Use the board's IP, not its .local name. mDNS resolution was measured at 5.1 s
per lookup on macOS with no caching, which blows straight past the hook's 2 s
timeout and makes every hook silently fail. Give the board a DHCP reservation
so the address is stable.

Merge the output into ~/.claude/settings.json. It contains two top-level keys:
"hooks" for the mood events, and "statusLine" for the usage gauge -- the
five-hour limit percentage and reset time exist only in the status line's JSON,
not in any hook payload.

Two things about the generated config are load-bearing:

  "args" present  -> exec form. Claude Code spawns curl directly with no shell,
                     so the config is byte-identical on macOS and Windows. Shell
                     form would go through sh on macOS but PowerShell on Windows
                     when Git Bash is absent, and PowerShell aliases `curl` to
                     Invoke-WebRequest, which takes entirely different flags.

  "async": true   -> fire and forget. Hook handlers block Claude Code's agentic
                     loop by default with a 600 s timeout, so an unreachable
                     board could otherwise stall a session for ten minutes.
                     `curl -m 2` bounds the call itself so dead-board processes
                     cannot pile up either.
"""

import json
import os
import shlex
import sys

# Claude Code hook -> matcher -> the raw fact the board is told about.
# WORKING is inferred board-side as the span between prompt_submitted and
# turn_finished. BORED and ASLEEP cannot come from hooks at all: idle_prompt
# fires once and never repeats, and nothing fires when Claude Code is not
# running. Those are elapsed-time decay on the board.
MAPPING = [
    ("UserPromptSubmit",   "",                  "prompt_submitted"),
    ("Stop",               "",                  "turn_finished"),

    # The three ways Claude actually asks the human for something. All of them
    # fire the instant the request is made, with no delay:
    #
    #   PermissionRequest  - fires only when Claude Code is about to show a
    #                        permission dialog. The docs are explicit that this
    #                        is narrower than PreToolUse, which runs before
    #                        every tool call whether or not permission is
    #                        needed. No matcher: any tool asking counts.
    #   PreToolUse         - matches on tool name, so scoping it to the two
    #                        tools that exist to ask the user costs nothing on
    #                        any other tool call.
    ("PermissionRequest",  "",                            "needs_input"),
    ("PreToolUse",         "AskUserQuestion|ExitPlanMode", "needs_input"),

    # ...and the matching clear, so the red field goes away as soon as the
    # question is answered rather than hanging around until the turn ends.
    ("PostToolUse",        "AskUserQuestion|ExitPlanMode", "answered"),

    # Fires ~60 s after a turn ends with no reply. Kept as a plain idle signal;
    # map it to needs_input instead if you want the face to nag.
    ("Notification",       "idle_prompt",       "idle"),

    # The delayed permission notification. Redundant now that
    # PermissionRequest is wired, but harmless: it re-asserts the same state.
    ("Notification",       "permission_prompt", "needs_input"),

    ("PostToolUseFailure", "*",                 "error"),

    # StopFailure's matcher is the API error type. rate_limit is the five-hour
    # usage limit; the rest are ordinary failures. Splitting them means the
    # board can show something you cannot fix by answering a prompt.
    ("StopFailure",        "rate_limit",        "limit_reached"),
    ("StopFailure",        "overloaded|authentication_failed|billing_error|"
                           "oauth_org_not_allowed|invalid_request|"
                           "model_not_found|server_error|max_output_tokens|"
                           "unknown",           "error"),
    ("SessionStart",       "",                  "session_started"),
    ("SessionEnd",         "",                  "session_ended"),
]


def entry(host: str, token: str, event: str) -> dict:
    return {
        "type": "command",
        "command": "curl",
        "args": [
            "-s", "-m", "2", "-X", "POST",
            "-H", f"Authorization: Bearer {token}",
            f"http://{host}/e/{event}",
        ],
        "async": True,
        "timeout": 5,
    }


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    host, token = sys.argv[1], sys.argv[2]

    hooks: dict = {}
    for hook_name, matcher, event in MAPPING:
        hooks.setdefault(hook_name, []).append(
            {"matcher": matcher, "hooks": [entry(host, token, event)]}
        )

    # The status line is the only place Claude Code exposes rate-limit figures,
    # so the usage gauge depends on it. refreshInterval keeps the countdown
    # moving while you are idle, when no assistant message would trigger it.
    # statusLine takes a shell string, not an argv array, so the path has to be
    # quoted -- this project's own directory has spaces in it.
    here = os.path.dirname(os.path.abspath(__file__))
    script = shlex.quote(os.path.join(here, "statusline.py"))
    statusline = {
        "type": "command",
        "command": f"python3 {script} {shlex.quote(host)} {shlex.quote(token)}",
        "refreshInterval": 60,
    }

    print(json.dumps({"hooks": hooks, "statusLine": statusline}, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
