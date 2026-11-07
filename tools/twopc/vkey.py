#!/usr/bin/env python3
"""Tap keys on a temporary uinput keyboard, for driving game menus on a Linux test PC.

Usage: vkey.py STEP [STEP ...]
  KEY          tap a key (esc, enter, up, down, left, right, space, tab, a-z, 0-9, f1-f12)
  KEY:MS       hold the key for MS milliseconds
  sleep:MS     wait MS milliseconds

The device exists only while the command runs, so no key can stay pressed afterwards. Needs
python-evdev and write access to /dev/uinput (the logged-in seat user normally has it). Input goes
to whatever has focus, so use it only on a dedicated test PC.
"""
from __future__ import annotations

import sys
import time

ALIASES = {"esc": "ESC", "escape": "ESC", "enter": "ENTER", "return": "ENTER", "up": "UP",
           "down": "DOWN", "left": "LEFT", "right": "RIGHT", "space": "SPACE", "tab": "TAB",
           "backspace": "BACKSPACE"}
MAX_STEPS = 64
MAX_HOLD_MS = 2000
MAX_SLEEP_MS = 60000
DEFAULT_HOLD_MS = 80
GAP_SECONDS = 0.35  # menus ignore taps that arrive faster than their own repeat delay


def key_name(word: str) -> str:
    name = ALIASES.get(word.lower(), word.upper())
    valid = (len(name) == 1 and name.isalnum()) or name in ALIASES.values() or (
        name.startswith("F") and name[1:].isdigit() and 1 <= int(name[1:]) <= 12)
    if not valid:
        raise ValueError(f"unknown key: {word}")
    return name


def parse_steps(args: list[str]) -> list[tuple[str, str, int]]:
    """Returns ("key", NAME, hold_ms) and ("sleep", "", ms) steps; raises ValueError on bad input."""
    if not args:
        raise ValueError("no steps given")
    if len(args) > MAX_STEPS:
        raise ValueError(f"at most {MAX_STEPS} steps")
    steps = []
    for arg in args:
        word, sep, value = arg.partition(":")
        if sep and not value.isdigit():
            raise ValueError(f"bad duration in {arg}")
        if word.lower() == "sleep":
            ms = int(value) if sep else 0
            if not 0 < ms <= MAX_SLEEP_MS:
                raise ValueError(f"sleep must be 1..{MAX_SLEEP_MS} ms")
            steps.append(("sleep", "", ms))
            continue
        ms = int(value) if sep else DEFAULT_HOLD_MS
        if not 0 < ms <= MAX_HOLD_MS:
            raise ValueError(f"hold must be 1..{MAX_HOLD_MS} ms")
        steps.append(("key", key_name(word), ms))
    return steps


def run(steps: list[tuple[str, str, int]]) -> None:
    from evdev import UInput, ecodes  # imported here so parsing works without evdev

    codes = sorted({getattr(ecodes, "KEY_" + name) for kind, name, _ in steps if kind == "key"})
    device = UInput({ecodes.EV_KEY: codes}, name="theys-gone-vkey")
    try:
        time.sleep(1.0)  # let the compositor pick the new device up
        for kind, name, ms in steps:
            if kind == "sleep":
                time.sleep(ms / 1000)
                continue
            code = getattr(ecodes, "KEY_" + name)
            device.write(ecodes.EV_KEY, code, 1)
            device.syn()
            try:
                time.sleep(ms / 1000)
            finally:
                device.write(ecodes.EV_KEY, code, 0)
                device.syn()
            time.sleep(GAP_SECONDS)
    finally:
        device.close()


def main(argv: list[str]) -> int:
    try:
        steps = parse_steps(argv)
    except ValueError as error:
        print(f"vkey: {error}", file=sys.stderr)
        return 2
    run(steps)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
