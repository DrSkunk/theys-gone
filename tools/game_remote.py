#!/usr/bin/env python3
"""Authenticated, allowlisted game-session screenshots and input for Linux/Windows."""
from __future__ import annotations

import argparse
import ctypes
import http.client
import ipaddress
import json
import os
from pathlib import Path
import secrets
import shutil
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse

MAX_BODY_BYTES = 8192
MAX_SCREENSHOT_BYTES = 12 * 1024 * 1024
MAX_ACTIONS = 24
MAX_DURATION_MS = 2000
MAX_MOVE_DELTA = 1000
YDOTOOL_TIMEOUT = 4
CAPTURE_TIMEOUT = 8

KEYCODES = {
    **{chr(ord("a") + index): code for index, code in enumerate(
        (30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50, 49, 24, 25,
         16, 19, 31, 20, 22, 47, 17, 45, 21, 44))},
    **{str(index): code for index, code in enumerate(
        (11, 2, 3, 4, 5, 6, 7, 8, 9, 10))},
    "enter": 28, "escape": 1, "space": 57, "tab": 15, "backspace": 14,
    "left": 105, "right": 106, "up": 103, "down": 108,
    "home": 102, "end": 107, "pageup": 104, "pagedown": 109,
    "shift": 42, "ctrl": 29, "alt": 56,
    **{f"f{index}": code for index, code in zip(range(1, 13),
                                                (*range(59, 69), 87, 88))},
}
MOUSE_BUTTONS = {"left": 0, "right": 1, "middle": 2}
GAMEPAD_BUTTONS = {
    "south": "BTN_SOUTH", "east": "BTN_EAST", "west": "BTN_WEST",
    "north": "BTN_NORTH", "lb": "BTN_TL", "rb": "BTN_TR",
    "back": "BTN_SELECT", "start": "BTN_START",
    "left_stick": "BTN_THUMBL", "right_stick": "BTN_THUMBR",
}


class RequestError(ValueError):
    pass


def read_token(path: Path) -> str:
    if os.name == "posix":
        mode = path.stat().st_mode & 0o777
        if mode & 0o077:
            raise ValueError(f"token file permissions must be 0600 or stricter: {path}")
    token = path.read_text(encoding="ascii").strip()
    if len(token) < 32:
        raise ValueError("token must contain at least 32 characters")
    return token


def create_token(path: Path) -> None:
    token = secrets.token_urlsafe(48)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    descriptor = os.open(path, flags, 0o600)
    with os.fdopen(descriptor, "w", encoding="ascii") as stream:
        stream.write(token + "\n")
    print(f"Created token file: {path}")


def validate_actions(value: object) -> list[dict[str, object]]:
    if not isinstance(value, list) or not 1 <= len(value) <= MAX_ACTIONS:
        raise RequestError(f"actions must be a list of 1 to {MAX_ACTIONS} items")
    actions: list[dict[str, object]] = []
    for item in value:
        if not isinstance(item, dict) or not isinstance(item.get("type"), str):
            raise RequestError("each action must be an object with a type")
        kind = item["type"]
        if kind == "key":
            key = item.get("key")
            duration = item.get("duration_ms", 100)
            if not isinstance(key, str) or key.lower() not in KEYCODES:
                raise RequestError(f"unsupported key: {key!r}")
            if type(duration) is not int or not 20 <= duration <= MAX_DURATION_MS:
                raise RequestError(f"duration_ms must be 20..{MAX_DURATION_MS}")
            actions.append({"type": kind, "key": key.lower(), "duration_ms": duration})
        elif kind == "mouse_move":
            dx, dy = item.get("dx"), item.get("dy")
            if (type(dx) is not int or type(dy) is not int
                    or abs(dx) > MAX_MOVE_DELTA or abs(dy) > MAX_MOVE_DELTA):
                raise RequestError(f"dx and dy must be integers in +/-{MAX_MOVE_DELTA}")
            actions.append({"type": kind, "dx": dx, "dy": dy})
        elif kind == "mouse_click":
            button = item.get("button", "left")
            duration = item.get("duration_ms", 100)
            if not isinstance(button, str) or button not in MOUSE_BUTTONS:
                raise RequestError(f"unsupported mouse button: {button!r}")
            if type(duration) is not int or not 20 <= duration <= MAX_DURATION_MS:
                raise RequestError(f"duration_ms must be 20..{MAX_DURATION_MS}")
            actions.append({"type": kind, "button": button, "duration_ms": duration})
        elif kind == "gamepad_button":
            button = item.get("button")
            duration = item.get("duration_ms", 100)
            if not isinstance(button, str) or button not in GAMEPAD_BUTTONS:
                raise RequestError(f"unsupported gamepad button: {button!r}")
            if type(duration) is not int or not 20 <= duration <= MAX_DURATION_MS:
                raise RequestError(f"duration_ms must be 20..{MAX_DURATION_MS}")
            actions.append({"type": kind, "button": button, "duration_ms": duration})
        elif kind == "gamepad_stick":
            stick, x, y = item.get("stick"), item.get("x"), item.get("y")
            duration = item.get("duration_ms", 100)
            if stick not in ("left", "right"):
                raise RequestError("stick must be left or right")
            if (type(x) is not int or type(y) is not int
                    or not -32768 <= x <= 32767 or not -32768 <= y <= 32767):
                raise RequestError("gamepad stick x and y must be integers in -32768..32767")
            if type(duration) is not int or not 20 <= duration <= MAX_DURATION_MS:
                raise RequestError(f"duration_ms must be 20..{MAX_DURATION_MS}")
            actions.append({"type": kind, "stick": stick, "x": x, "y": y,
                            "duration_ms": duration})
        elif kind == "gamepad_trigger":
            side, value = item.get("side"), item.get("value")
            duration = item.get("duration_ms", 100)
            if side not in ("left", "right"):
                raise RequestError("trigger side must be left or right")
            if type(value) is not int or not 0 <= value <= 255:
                raise RequestError("trigger value must be an integer in 0..255")
            if type(duration) is not int or not 20 <= duration <= MAX_DURATION_MS:
                raise RequestError(f"duration_ms must be 20..{MAX_DURATION_MS}")
            actions.append({"type": kind, "side": side, "value": value,
                            "duration_ms": duration})
        elif kind == "wait":
            duration = item.get("duration_ms")
            if type(duration) is not int or not 0 <= duration <= MAX_DURATION_MS:
                raise RequestError(f"wait duration_ms must be 0..{MAX_DURATION_MS}")
            actions.append({"type": kind, "duration_ms": duration})
        else:
            raise RequestError(f"unsupported action type: {kind!r}")
    if sum(int(action.get("duration_ms", 0)) for action in actions) > 10_000:
        raise RequestError("total requested wait/key time must not exceed 10 seconds")
    return actions


class SessionInput:
    name = ""

    def __init__(self):
        self._lock = threading.Lock()
        self._held_keys: set[int] = set()
        self._held_mouse_buttons: set[int] = set()
        self._gamepad = None
        self.gamepad_error = ""

    @property
    def gamepad_available(self) -> bool:
        return self._gamepad is not None

    def _key_event(self, code: int, down: bool) -> None:
        raise NotImplementedError

    def _mouse_event(self, button: int, down: bool) -> None:
        raise NotImplementedError

    def _mouse_move(self, dx: int, dy: int) -> None:
        raise NotImplementedError

    def _gamepad_tap(self, button: str, duration_ms: int) -> None:
        raise NotImplementedError

    def _gamepad_stick(self, stick: str, x: int, y: int, duration_ms: int) -> None:
        raise NotImplementedError

    def _gamepad_trigger(self, side: str, value: int, duration_ms: int) -> None:
        raise NotImplementedError

    def close(self) -> None:
        raise NotImplementedError

    def execute(self, actions: list[dict[str, object]]) -> None:
        with self._lock:
            try:
                for action in actions:
                    kind = action["type"]
                    if kind == "key":
                        code = KEYCODES[str(action["key"])]
                        self._held_keys.add(code)
                        try:
                            self._key_event(code, True)
                            time.sleep(int(action["duration_ms"]) / 1000)
                        finally:
                            self._key_event(code, False)
                            self._held_keys.discard(code)
                    elif kind == "mouse_move":
                        self._mouse_move(int(action["dx"]), int(action["dy"]))
                    elif kind == "mouse_click":
                        button = MOUSE_BUTTONS[str(action["button"])]
                        self._held_mouse_buttons.add(button)
                        try:
                            self._mouse_event(button, True)
                            time.sleep(int(action["duration_ms"]) / 1000)
                        finally:
                            self._mouse_event(button, False)
                            self._held_mouse_buttons.discard(button)
                    elif kind == "gamepad_button":
                        self._gamepad_tap(str(action["button"]),
                                          int(action["duration_ms"]))
                    elif kind == "gamepad_stick":
                        self._gamepad_stick(str(action["stick"]), int(action["x"]),
                                            int(action["y"]), int(action["duration_ms"]))
                    elif kind == "gamepad_trigger":
                        self._gamepad_trigger(str(action["side"]), int(action["value"]),
                                              int(action["duration_ms"]))
                    elif kind == "wait":
                        time.sleep(int(action["duration_ms"]) / 1000)
            finally:
                self.release_all()

    def _require_gamepad(self):
        if self._gamepad is None:
            detail = f": {self.gamepad_error}" if self.gamepad_error else ""
            raise OSError(f"virtual gamepad unavailable{detail}")
        return self._gamepad

    def release_all(self) -> None:
        for code in tuple(self._held_keys):
            self._key_event(code, False)
            self._held_keys.discard(code)
        for button in tuple(self._held_mouse_buttons):
            self._mouse_event(button, False)
            self._held_mouse_buttons.discard(button)


class LinuxInput(SessionInput):
    name = "ydotool"

    def __init__(self, ydotool: str):
        super().__init__()
        self.ydotool = ydotool
        self._gamepad_codes: dict[str, int] = {}
        try:
            from evdev import AbsInfo, UInput, ecodes

            axes = {
                ecodes.ABS_X: AbsInfo(0, -32768, 32767, 0, 0, 0),
                ecodes.ABS_Y: AbsInfo(0, -32768, 32767, 0, 0, 0),
                ecodes.ABS_RX: AbsInfo(0, -32768, 32767, 0, 0, 0),
                ecodes.ABS_RY: AbsInfo(0, -32768, 32767, 0, 0, 0),
                ecodes.ABS_Z: AbsInfo(0, 0, 255, 0, 0, 0),
                ecodes.ABS_RZ: AbsInfo(0, 0, 255, 0, 0, 0),
            }
            self._gamepad_codes = {
                name: getattr(ecodes, code_name)
                for name, code_name in GAMEPAD_BUTTONS.items()
            }
            self._gamepad = UInput(
                {ecodes.EV_KEY: list(self._gamepad_codes.values()),
                 ecodes.EV_ABS: list(axes.items())},
                name="Theys Gone Remote Gamepad",
                vendor=0x1209, product=0xD00D,
            )
            self._evdev_ecodes = ecodes
        except (ImportError, OSError) as error:
            self.gamepad_error = str(error)

    def _run(self, *args: str) -> None:
        subprocess.run((self.ydotool, *args), check=True, timeout=YDOTOOL_TIMEOUT,
                       stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                       stderr=subprocess.PIPE)

    def _key_event(self, code: int, down: bool) -> None:
        self._run("key", f"{code}:{int(down)}")

    def _mouse_event(self, button: int, down: bool) -> None:
        self._run("click", hex((0x40 if down else 0x80) | button))

    def _mouse_move(self, dx: int, dy: int) -> None:
        self._run("mousemove", "-x", str(dx), "-y", str(dy))

    def _gamepad_tap(self, button: str, duration_ms: int) -> None:
        device = self._require_gamepad()
        code = self._gamepad_codes[button]
        try:
            device.write(self._evdev_ecodes.EV_KEY, code, 1)
            device.syn()
            time.sleep(duration_ms / 1000)
        finally:
            device.write(self._evdev_ecodes.EV_KEY, code, 0)
            device.syn()

    def _gamepad_stick(self, stick: str, x: int, y: int, duration_ms: int) -> None:
        device = self._require_gamepad()
        codes = self._evdev_ecodes
        x_axis, y_axis = ((codes.ABS_X, codes.ABS_Y) if stick == "left"
                          else (codes.ABS_RX, codes.ABS_RY))
        try:
            device.write(codes.EV_ABS, x_axis, x)
            device.write(codes.EV_ABS, y_axis, y)
            device.syn()
            time.sleep(duration_ms / 1000)
        finally:
            device.write(codes.EV_ABS, x_axis, 0)
            device.write(codes.EV_ABS, y_axis, 0)
            device.syn()

    def _gamepad_trigger(self, side: str, value: int, duration_ms: int) -> None:
        device = self._require_gamepad()
        axis = self._evdev_ecodes.ABS_Z if side == "left" else self._evdev_ecodes.ABS_RZ
        try:
            device.write(self._evdev_ecodes.EV_ABS, axis, value)
            device.syn()
            time.sleep(duration_ms / 1000)
        finally:
            device.write(self._evdev_ecodes.EV_ABS, axis, 0)
            device.syn()

    def release_all(self) -> None:
        super().release_all()
        if self._gamepad is not None:
            codes = self._evdev_ecodes
            for button in self._gamepad_codes.values():
                self._gamepad.write(codes.EV_KEY, button, 0)
            for axis in (codes.ABS_X, codes.ABS_Y, codes.ABS_RX, codes.ABS_RY,
                         codes.ABS_Z, codes.ABS_RZ):
                self._gamepad.write(codes.EV_ABS, axis, 0)
            self._gamepad.syn()

    def close(self) -> None:
        try:
            self.release_all()
        finally:
            if self._gamepad is not None:
                self._gamepad.close()
                self._gamepad = None


class WinMouseInput(ctypes.Structure):
    _fields_ = [("dx", ctypes.c_int32), ("dy", ctypes.c_int32),
                ("mouseData", ctypes.c_uint32), ("dwFlags", ctypes.c_uint32),
                ("time", ctypes.c_uint32), ("dwExtraInfo", ctypes.c_size_t)]


class WinKeyboardInput(ctypes.Structure):
    _fields_ = [("wVk", ctypes.c_uint16), ("wScan", ctypes.c_uint16),
                ("dwFlags", ctypes.c_uint32), ("time", ctypes.c_uint32),
                ("dwExtraInfo", ctypes.c_size_t)]


class WinHardwareInput(ctypes.Structure):
    _fields_ = [("uMsg", ctypes.c_uint32), ("wParamL", ctypes.c_uint16),
                ("wParamH", ctypes.c_uint16)]


class WinInputData(ctypes.Union):
    _fields_ = [("mi", WinMouseInput), ("ki", WinKeyboardInput),
                ("hi", WinHardwareInput)]


class WinInput(ctypes.Structure):
    _fields_ = [("type", ctypes.c_uint32), ("data", WinInputData)]


class WindowsInput(SessionInput):
    name = "SendInput"

    def __init__(self, *, user32=None, enable_gamepad: bool = False):
        super().__init__()
        self._user32 = user32 if user32 is not None else ctypes.WinDLL(
            "user32", use_last_error=True)
        self._user32.SendInput.argtypes = (
            ctypes.c_uint32, ctypes.POINTER(WinInput), ctypes.c_int)
        self._user32.SendInput.restype = ctypes.c_uint32
        self.gamepad_error = "not enabled; use --windows-gamepad with vgamepad/ViGEmBus installed"
        if enable_gamepad:
            try:
                import vgamepad
            except ImportError as error:
                raise ValueError("Windows gamepad needs the remote-windows Poetry extra") from error
            self._gamepad = vgamepad.VX360Gamepad()
            names = {
                "south": "A", "east": "B", "west": "X", "north": "Y",
                "lb": "LEFT_SHOULDER", "rb": "RIGHT_SHOULDER",
                "back": "BACK", "start": "START",
                "left_stick": "LEFT_THUMB", "right_stick": "RIGHT_THUMB",
            }
            self._gamepad_buttons = {
                name: getattr(vgamepad.XUSB_BUTTON, "XUSB_GAMEPAD_" + suffix)
                for name, suffix in names.items()
            }
            self.gamepad_error = ""

    def _send(self, event: WinInput) -> None:
        if self._user32.SendInput(1, ctypes.byref(event), ctypes.sizeof(WinInput)) != 1:
            raise OSError("SendInput failed; use the same desktop and elevation level as the game")

    def _key_event(self, code: int, down: bool) -> None:
        extended = {102: 0x47, 103: 0x48, 104: 0x49, 105: 0x4B,
                    106: 0x4D, 107: 0x4F, 108: 0x50, 109: 0x51}
        flags = 0x0008 | (0 if down else 0x0002) | (1 if code in extended else 0)
        self._send(WinInput(type=1, data=WinInputData(
            ki=WinKeyboardInput(wScan=extended.get(code, code), dwFlags=flags))))

    def _mouse_event(self, button: int, down: bool) -> None:
        flags = ((0x0002, 0x0004), (0x0008, 0x0010), (0x0020, 0x0040))
        self._send(WinInput(type=0, data=WinInputData(
            mi=WinMouseInput(dwFlags=flags[button][0 if down else 1]))))

    def _mouse_move(self, dx: int, dy: int) -> None:
        self._send(WinInput(type=0, data=WinInputData(
            mi=WinMouseInput(dx=dx, dy=dy, dwFlags=0x0001))))

    def _gamepad_tap(self, button: str, duration_ms: int) -> None:
        device = self._require_gamepad()
        try:
            device.press_button(button=self._gamepad_buttons[button])
            device.update()
            time.sleep(duration_ms / 1000)
        finally:
            device.release_button(button=self._gamepad_buttons[button])
            device.update()

    def _gamepad_stick(self, stick: str, x: int, y: int, duration_ms: int) -> None:
        device = self._require_gamepad()
        move = device.left_joystick if stick == "left" else device.right_joystick
        try:
            # The API uses Linux's down-positive Y; XInput uses up-positive Y.
            move(x_value=x, y_value=max(-32768, min(32767, -y)))
            device.update()
            time.sleep(duration_ms / 1000)
        finally:
            move(x_value=0, y_value=0)
            device.update()

    def _gamepad_trigger(self, side: str, value: int, duration_ms: int) -> None:
        device = self._require_gamepad()
        trigger = device.left_trigger if side == "left" else device.right_trigger
        try:
            trigger(value=value)
            device.update()
            time.sleep(duration_ms / 1000)
        finally:
            trigger(value=0)
            device.update()

    def release_all(self) -> None:
        super().release_all()
        if self._gamepad is not None:
            self._gamepad.reset()
            self._gamepad.update()

    def close(self) -> None:
        try:
            self.release_all()
        finally:
            self._gamepad = None


WINDOWS_CAPTURE_SCRIPT = """
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type 'using System.Runtime.InteropServices; public class CaptureDpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}'
[CaptureDpi]::SetProcessDPIAware() | Out-Null
$bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bitmap = [System.Drawing.Bitmap]::new($bounds.Width, $bounds.Height)
try {
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $bounds.Size)
        $bitmap.Save($env:THEYS_GONE_CAPTURE, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose() }
} finally { $bitmap.Dispose() }
"""


class ScreenshotCapture:
    def __init__(self, tool: str):
        if tool not in ("grim", "scrot", "spectacle", "ffmpeg-x11", "gamescope", "windows"):
            raise ValueError("unsupported screenshot tool")
        binary = {"windows": "powershell.exe", "ffmpeg-x11": "ffmpeg",
                  "gamescope": "gamescopectl"}.get(tool, tool)
        path = shutil.which(binary)
        if not path:
            raise ValueError(f"required screenshot tool not found: {tool}")
        self.path = path
        self.tool = tool
        self._lock = threading.Lock()

    def capture(self) -> bytes:
        with self._lock, tempfile.TemporaryDirectory(prefix="game-remote-") as directory:
            image = Path(directory) / "screen.png"
            if self.tool == "windows":
                command = (self.path, "-NoProfile", "-NonInteractive", "-Command",
                           WINDOWS_CAPTURE_SCRIPT)
            elif self.tool == "spectacle":
                command = (self.path, "--background", "--nonotify", "--fullscreen", "--pointer",
                           "--output", str(image))
            elif self.tool == "gamescope":
                if not os.environ.get("WAYLAND_DISPLAY"):
                    raise ValueError("gamescope capture requires the compositor's WAYLAND_DISPLAY")
                command = (self.path, "screenshot", str(image))
            elif self.tool == "ffmpeg-x11":
                display = os.environ.get("DISPLAY")
                if not display:
                    raise ValueError("ffmpeg-x11 capture requires DISPLAY and the session's XAUTHORITY")
                command = (self.path, "-hide_banner", "-loglevel", "error", "-f",
                           "x11grab", "-i", display, "-frames:v", "1", "-y", str(image))
            else:
                command = (self.path, str(image))
            environment = dict(os.environ, THEYS_GONE_CAPTURE=str(image))
            deadline = time.monotonic() + CAPTURE_TIMEOUT
            try:
                subprocess.run(command, check=True, timeout=CAPTURE_TIMEOUT, env=environment,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.PIPE)
            except subprocess.CalledProcessError as error:
                detail = (error.stderr or b"").decode("utf-8", "replace")[:2048]
                raise OSError(f"{self.tool} capture failed: {detail}") from error
            if self.tool == "gamescope":
                while time.monotonic() < deadline:
                    if image.exists() and image.stat().st_size >= 12:
                        if image.stat().st_size > MAX_SCREENSHOT_BYTES:
                            raise ValueError("screenshot exceeds the 12 MiB response limit")
                        with image.open("rb") as stream:
                            stream.seek(-12, os.SEEK_END)
                            if stream.read() == b"\x00\x00\x00\x00IEND\xaeB`\x82":
                                break
                    time.sleep(0.05)
                else:
                    raise TimeoutError("Gamescope did not finish writing its PNG screenshot")
            size = image.stat().st_size
            if size > MAX_SCREENSHOT_BYTES:
                raise ValueError("screenshot exceeds the 12 MiB response limit")
            data = image.read_bytes()
            if not data.startswith(b"\x89PNG\r\n\x1a\n"):
                raise ValueError("capture tool did not produce a PNG screenshot")
            return data


def is_loopback(host: str) -> bool:
    try:
        return ipaddress.ip_address(host).is_loopback
    except ValueError:
        return host.lower() == "localhost"


def make_server(host: str, port: int, token: str, capture: ScreenshotCapture,
                input_backend: SessionInput) -> ThreadingHTTPServer:
    class Handler(BaseHTTPRequestHandler):
        server_version = "GameRemote/1"

        def log_message(self, fmt: str, *args: object) -> None:
            print(f"game-remote: {self.client_address[0]} {fmt % args}")

        def _authorized(self) -> bool:
            supplied = self.headers.get("Authorization", "")
            return secrets.compare_digest(supplied, f"Bearer {token}")

        def _json(self, status: int, payload: dict[str, object]) -> None:
            body = json.dumps(payload).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            if not self._authorized():
                self._json(401, {"error": "unauthorized"})
                return
            if self.path == "/v1/health":
                self._json(200, {
                    "status": "ok", "input": input_backend.name,
                    "gamepad": input_backend.gamepad_available,
                    "gamepad_error": input_backend.gamepad_error or None,
                    "screenshot": capture.tool,
                })
                return
            if self.path == "/v1/screenshot":
                try:
                    image = capture.capture()
                except (OSError, subprocess.SubprocessError, ValueError) as error:
                    self._json(503, {"error": str(error)})
                    return
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(image)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(image)
                return
            self._json(404, {"error": "not found"})

        def do_POST(self) -> None:
            if not self._authorized():
                self._json(401, {"error": "unauthorized"})
                return
            if self.path != "/v1/actions":
                self._json(404, {"error": "not found"})
                return
            try:
                length = int(self.headers.get("Content-Length", "-1"))
                if not 0 <= length <= MAX_BODY_BYTES:
                    raise RequestError("request body must be at most 8 KiB")
                actions = validate_actions(json.loads(self.rfile.read(length)))
                input_backend.execute(actions)
            except (ValueError, json.JSONDecodeError) as error:
                self._json(400, {"error": str(error)})
                return
            except (OSError, subprocess.SubprocessError) as error:
                self._json(503, {"error": f"input backend failed: {error}"})
                return
            self._json(200, {"status": "ok", "actions": len(actions)})

    server = ThreadingHTTPServer((host, port), Handler)
    server.daemon_threads = True
    return server


def create_backends(capture: str, windows_gamepad: bool = False) -> tuple[ScreenshotCapture, SessionInput]:
    if sys.platform == "win32":
        if capture not in ("auto", "windows"):
            raise ValueError("Windows capture must be auto or windows")
        return ScreenshotCapture("windows"), WindowsInput(enable_gamepad=windows_gamepad)
    if not sys.platform.startswith("linux"):
        raise ValueError("serving is supported only on Linux and Windows; control works on other platforms")
    if windows_gamepad or capture == "windows":
        raise ValueError("--windows-gamepad and --capture windows require Windows")
    screenshot = ScreenshotCapture("grim" if capture == "auto" else capture)
    ydotool = shutil.which("ydotool")
    if not ydotool:
        raise ValueError("ydotool not found; install ydotool and start ydotoold")
    return screenshot, LinuxInput(ydotool)


def serve(args: argparse.Namespace) -> None:
    if not is_loopback(args.host) and not (args.tls_cert and args.tls_key):
        raise ValueError("non-loopback listeners require TLS (--tls-cert and --tls-key)")
    token = read_token(args.token_file)
    context = None
    if args.tls_cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        context.load_cert_chain(args.tls_cert, args.tls_key)
    capture, input_backend = create_backends(args.capture, args.windows_gamepad)
    try:
        server = make_server(args.host, args.port, token, capture, input_backend)
    except OSError:
        input_backend.close()
        raise
    if context:
        try:
            server.socket = context.wrap_socket(server.socket, server_side=True)
        except OSError:
            server.server_close()
            input_backend.close()
            raise
    print(f"game-remote listening on {args.host}:{args.port} (TLS={'on' if args.tls_cert else 'off'})")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        try:
            input_backend.close()
        finally:
            server.server_close()


def control(args: argparse.Namespace) -> None:
    parsed = urlparse(args.url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname:
        raise ValueError("--url must be an http(s) URL")
    if parsed.scheme == "http" and not is_loopback(parsed.hostname):
        raise ValueError("HTTP is allowed only for loopback; use HTTPS for remote PCs")
    token = read_token(args.token_file)
    context = None
    if parsed.scheme == "https":
        context = ssl.create_default_context(cafile=str(args.ca_cert) if args.ca_cert else None)
        context.minimum_version = ssl.TLSVersion.TLSv1_2
    connection_type = http.client.HTTPSConnection if context else http.client.HTTPConnection
    kwargs: dict[str, object] = {"timeout": 12}
    if context:
        kwargs["context"] = context
    connection = connection_type(parsed.hostname, parsed.port, **kwargs)
    path = parsed.path.rstrip("/")
    if args.operation == "health":
        endpoint, method, body = f"{path}/v1/health", "GET", None
    elif args.operation == "screenshot":
        endpoint, method, body = f"{path}/v1/screenshot", "GET", None
    else:
        endpoint, method = f"{path}/v1/actions", "POST"
        body = json.dumps(json.loads(args.actions))
        validate_actions(json.loads(body))
    connection.request(method, endpoint, body=body, headers={
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
    })
    response = connection.getresponse()
    data = response.read(MAX_SCREENSHOT_BYTES + 1)
    connection.close()
    if response.status != 200:
        raise RuntimeError(f"server returned HTTP {response.status}: {data[:2048].decode('utf-8', 'replace')}")
    if args.operation == "screenshot":
        if len(data) > MAX_SCREENSHOT_BYTES or not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise RuntimeError("server returned an invalid or oversized PNG")
        args.output.write_bytes(data)
        print(f"Saved screenshot to {args.output}")
    else:
        print(data.decode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    token_parser = subparsers.add_parser("create-token", help="create a private bearer-token file")
    token_parser.add_argument("token_file", type=Path)

    server_parser = subparsers.add_parser("serve", help="run the local game-session API")
    server_parser.add_argument("--host", default="127.0.0.1")
    server_parser.add_argument("--port", type=int, default=8765)
    server_parser.add_argument("--token-file", required=True, type=Path)
    server_parser.add_argument("--capture", choices=(
        "auto", "grim", "scrot", "spectacle", "ffmpeg-x11", "gamescope", "windows"), default="auto")
    server_parser.add_argument("--windows-gamepad", action="store_true",
                               help="enable optional vgamepad/ViGEmBus controller on Windows")
    server_parser.add_argument("--tls-cert", type=Path)
    server_parser.add_argument("--tls-key", type=Path)

    client_parser = subparsers.add_parser("control", help="send one bounded request to a helper")
    client_parser.add_argument("--url", required=True)
    client_parser.add_argument("--token-file", required=True, type=Path)
    client_parser.add_argument("--ca-cert", type=Path)
    operations = client_parser.add_subparsers(dest="operation", required=True)
    operations.add_parser("health")
    screenshot = operations.add_parser("screenshot")
    screenshot.add_argument("output", type=Path)
    actions = operations.add_parser("actions")
    actions.add_argument("actions", help='JSON list, e.g. \'[{"type":"key","key":"w","duration_ms":300}]\'')

    args = parser.parse_args()
    try:
        if args.command == "create-token":
            create_token(args.token_file)
        elif args.command == "serve":
            if not 1 <= args.port <= 65535:
                raise ValueError("port must be 1..65535")
            if bool(args.tls_cert) != bool(args.tls_key):
                raise ValueError("--tls-cert and --tls-key must be provided together")
            serve(args)
        else:
            control(args)
    except (OSError, ValueError, RuntimeError, ssl.SSLError,
            subprocess.SubprocessError) as error:
        parser.exit(1, f"game-remote: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
