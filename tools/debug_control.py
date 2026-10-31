"""Send local, allowlisted requests to a running opt-in Days Gone test build."""
from __future__ import annotations

import argparse
import configparser
import os
from pathlib import Path
import tempfile
import time

COMMANDS = (
    "inspect", "begin", "end", "status", "continue-title", "ui-probe", "god-on", "god-off", "spawn-single",
    "spawn-pack", "cleanup", "probe-player", "probe-enemy", "enemy-health-all-on", "kill-owned-enemy", "time-shift",
    "weather-next", "dump-reflection", "dump-objects", "trace-on", "trace-off",
    "ride-test",
    "reload-save",
    "story-probe",
    "story-diverge",
    "hit-enemy",
    "net-probe",
    "net-listen",
    "net-connect",
    "net-possess",
    "net-takeover",
    "net-diag",
    "net-auto",
    "net-nudge",
    "net-input",
    "net-walk",
    "mission-watch-on",
    "mission-watch-off",
    "mission-state",
    "mission-act",
    "stream-focus",
    "stream-probe",
    "move-by",
    "minimap-probe",
    "look-companion",
    "level-probe",
    "material-probe",
    "vehicle-probe",
    "spawn-actor",
    "time-probe",
)
ARG_COMMANDS = ("mission-act", "stream-focus", "move-by", "level-probe", "spawn-actor")
ARG_LIMIT = 240


def read_ini(path: Path) -> configparser.ConfigParser:
    parser = configparser.ConfigParser(interpolation=None)
    if path.exists():
        parser.read_string(path.read_text(encoding="utf-8-sig"))
    return parser


def validate_arg(command: str, arg: str) -> None:
    if arg and command not in ARG_COMMANDS:
        raise ValueError(f"{command} does not take an argument")
    if command in ARG_COMMANDS and not arg:
        raise ValueError(f"{command} needs an argument")
    if len(arg) > ARG_LIMIT or any(not (0x20 <= ord(ch) < 0x7f) for ch in arg) or ";" in arg:
        raise ValueError(f"The argument must be printable ASCII without ';', at most {ARG_LIMIT} characters")


def send(game_dir: Path, command: str, timeout: float = 20, arg: str = "") -> tuple[bool, str]:
    if command not in COMMANDS:
        raise ValueError(f"Unknown command: {command}")
    validate_arg(command, arg)
    if not (game_dir / "TheysGone.asi").is_file():
        raise ValueError("The selected directory does not contain TheysGone.asi")
    if not 0 < timeout <= 120:
        raise ValueError("Timeout must be greater than zero and at most 120 seconds")
    mailbox = game_dir / "TheysGone-debug.ini"
    result = game_dir / "TheysGone-debug-result.ini"
    lock = game_dir / "TheysGone-debug.lock"
    with lock.open("x", encoding="ascii"):
        pass
    temporary: Path | None = None
    try:
        previous = read_ini(mailbox).getint("Debug", "Request", fallback=0)
        acknowledged = read_ini(result).getint("Result", "Request", fallback=0)
        if not 0 <= previous <= 0x7fffffff or not 0 <= acknowledged <= 0x7fffffff:
            raise ValueError("Existing request identifiers are out of range")
        request = max(previous, acknowledged) % 0x7fffffff + 1
        while request in (previous, acknowledged):
            request = request % 0x7fffffff + 1
        with tempfile.NamedTemporaryFile(mode="w", encoding="ascii", dir=game_dir,
                                         prefix=".TheysGone-debug-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(f"[Debug]\nCommand={command}\nArg={arg}\nRequest={request}\n")
        os.replace(temporary, mailbox)
        temporary = None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            reply = read_ini(result)
            if reply.getint("Result", "Request", fallback=0) == request:
                state = reply.get("Result", "State", fallback="")
                if state not in ("ok", "error"):
                    raise ValueError("Runtime acknowledgement has an invalid state")
                return state == "ok", reply.get("Result", "Message", fallback="")
            time.sleep(0.1)
        raise TimeoutError("No acknowledgement: check that the game is loaded with DebugTools=1")
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        lock.unlink()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True, type=Path, help="Directory containing TheysGone.asi")
    parser.add_argument("--timeout", type=float, default=20)
    parser.add_argument("--arg", default="", help="Argument for mission-act: '<Function> [ObjectName|-] [Param=Value ...]'")
    parser.add_argument("command", choices=COMMANDS)
    args = parser.parse_args()
    try:
        success, message = send(args.game_dir, args.command, args.timeout, args.arg)
    except (OSError, ValueError, configparser.Error) as error:
        parser.exit(1, f"debug control: {error}\n")
    print(f"{'ok' if success else 'error'}: {message}")
    return 0 if success else 1


if __name__ == "__main__":
    raise SystemExit(main())
