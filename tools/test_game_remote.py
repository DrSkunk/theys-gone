import ctypes
import http.client
import json
import os
import subprocess
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

import game_remote


class ValidateActionsTests(unittest.TestCase):
    def test_accepts_bounded_input_actions(self):
        self.assertEqual(
            game_remote.validate_actions([
                {"type": "key", "key": "W", "duration_ms": 250},
                {"type": "mouse_move", "dx": -20, "dy": 30},
                {"type": "mouse_click", "button": "left", "duration_ms": 100},
                {"type": "gamepad_button", "button": "south", "duration_ms": 100},
                {"type": "gamepad_stick", "stick": "left", "x": 1000, "y": -1000,
                 "duration_ms": 250},
                {"type": "gamepad_trigger", "side": "right", "value": 255,
                 "duration_ms": 50},
                {"type": "wait", "duration_ms": 50},
            ]),
            [
                {"type": "key", "key": "w", "duration_ms": 250},
                {"type": "mouse_move", "dx": -20, "dy": 30},
                {"type": "mouse_click", "button": "left", "duration_ms": 100},
                {"type": "gamepad_button", "button": "south", "duration_ms": 100},
                {"type": "gamepad_stick", "stick": "left", "x": 1000, "y": -1000,
                 "duration_ms": 250},
                {"type": "gamepad_trigger", "side": "right", "value": 255,
                 "duration_ms": 50},
                {"type": "wait", "duration_ms": 50},
            ],
        )

    def test_rejects_unknown_or_unbounded_actions(self):
        invalid = (
            {},
            [{"type": "shell", "command": "whoami"}],
            [{"type": "key", "key": "not-a-key"}],
            [{"type": "key", "key": "w", "duration_ms": 2001}],
            [{"type": "mouse_move", "dx": 1001, "dy": 0}],
            [{"type": "mouse_click", "button": "side"}],
            [{"type": "mouse_click", "button": []}],
            [{"type": "wait", "duration_ms": 2001}],
            [{"type": "key", "key": "w", "duration_ms": True}],
            [{"type": "wait", "duration_ms": 600}, {"type": "key", "key": "w", "duration_ms": 2000}] * 5,
            [{"type": "wait", "duration_ms": 0}] * (game_remote.MAX_ACTIONS + 1),
            [{"type": "gamepad_button", "button": "unknown"}],
            [{"type": "gamepad_stick", "stick": "left", "x": True, "y": 0}],
            [{"type": "gamepad_trigger", "side": "left", "value": 256}],
        )
        for value in invalid:
            with self.subTest(value=value), self.assertRaises(game_remote.RequestError):
                game_remote.validate_actions(value)


class LinuxInputTests(unittest.TestCase):
    def test_emits_only_mapped_ydotool_operations(self):
        backend = game_remote.LinuxInput("/usr/bin/ydotool")
        with patch.object(game_remote.subprocess, "run") as run, \
                patch.object(game_remote.time, "sleep"):
            backend.execute([
                {"type": "key", "key": "w", "duration_ms": 100},
                {"type": "mouse_move", "dx": 4, "dy": -2},
                {"type": "mouse_click", "button": "right", "duration_ms": 100},
            ])
        calls = [call.args[0] for call in run.call_args_list]
        self.assertEqual(calls, [
            ("/usr/bin/ydotool", "key", "17:1"),
            ("/usr/bin/ydotool", "key", "17:0"),
            ("/usr/bin/ydotool", "mousemove", "-x", "4", "-y", "-2"),
            ("/usr/bin/ydotool", "click", "0x41"),
            ("/usr/bin/ydotool", "click", "0x81"),
        ])
        self.assertTrue(all(call.kwargs["check"] for call in run.call_args_list))

    def test_key_is_released_when_timing_or_backend_fails(self):
        backend = game_remote.LinuxInput("/usr/bin/ydotool")
        with patch.object(game_remote.subprocess, "run") as run, \
                patch.object(game_remote.time, "sleep", side_effect=RuntimeError("stop")):
            with self.assertRaisesRegex(RuntimeError, "stop"):
                backend.execute([{"type": "key", "key": "w", "duration_ms": 100}])
        self.assertEqual([call.args[0][-1] for call in run.call_args_list], ["17:1", "17:0"])
        self.assertEqual(backend._held_keys, set())

    def test_gamepad_events_return_to_neutral(self):
        backend = game_remote.LinuxInput("/usr/bin/ydotool")
        codes = type("Codes", (), {
            "EV_KEY": 1, "EV_ABS": 3, "ABS_X": 0, "ABS_Y": 1,
            "ABS_RX": 3, "ABS_RY": 4, "ABS_Z": 2, "ABS_RZ": 5,
        })

        class Device:
            def __init__(self):
                self.events = []

            def write(self, *event):
                self.events.append(event)

            def syn(self):
                self.events.append(("syn",))

        device = Device()
        backend._gamepad = device
        backend._evdev_ecodes = codes
        backend._gamepad_codes = {"south": 304}
        with patch.object(game_remote.time, "sleep"):
            backend.execute([
                {"type": "gamepad_button", "button": "south", "duration_ms": 50},
                {"type": "gamepad_stick", "stick": "left", "x": 100, "y": -100,
                 "duration_ms": 50},
                {"type": "gamepad_trigger", "side": "right", "value": 255,
                 "duration_ms": 50},
            ])
        self.assertIn((1, 304, 1), device.events)
        self.assertIn((1, 304, 0), device.events)
        self.assertIn((3, 0, 100), device.events)
        self.assertIn((3, 1, -100), device.events)
        self.assertIn((3, 5, 255), device.events)
        self.assertTrue(device.events[-1] == ("syn",))


class TokenTests(unittest.TestCase):
    def test_token_creation_is_exclusive_and_private(self):
        with tempfile.TemporaryDirectory() as directory:
            token_path = Path(directory) / "token"
            game_remote.create_token(token_path)
            self.assertGreaterEqual(len(game_remote.read_token(token_path)), 32)
            if os.name == "posix":
                self.assertEqual(token_path.stat().st_mode & 0o777, 0o600)
            with self.assertRaises(FileExistsError):
                game_remote.create_token(token_path)

    @unittest.skipUnless(os.name == "posix", "Windows uses ACLs rather than POSIX modes")
    def test_rejects_group_readable_token(self):
        with tempfile.TemporaryDirectory() as directory:
            token_path = Path(directory) / "token"
            token_path.write_text("a" * 48)
            token_path.chmod(0o644)
            with self.assertRaisesRegex(ValueError, "permissions"):
                game_remote.read_token(token_path)


class WindowsInputTests(unittest.TestCase):
    def setUp(self):
        self.events = []

        def send(count, pointer, size):
            self.assertEqual(count, 1)
            self.assertEqual(size, ctypes.sizeof(game_remote.WinInput))
            self.events.append(game_remote.WinInput.from_buffer_copy(
                ctypes.string_at(pointer, size)))
            return 1

        self.user32 = SimpleNamespace(SendInput=Mock(side_effect=send))
        self.backend = game_remote.WindowsInput(user32=self.user32)

    def test_native_input_layout_and_scan_codes(self):
        expected_size = 40 if ctypes.sizeof(ctypes.c_void_p) == 8 else 28
        self.assertEqual(ctypes.sizeof(game_remote.WinInput), expected_size)
        with patch.object(game_remote.time, "sleep"):
            self.backend.execute(game_remote.validate_actions([
                {"type": "key", "key": "w"},
                {"type": "key", "key": "left"},
                {"type": "mouse_move", "dx": -15, "dy": 8},
                {"type": "mouse_click", "button": "right"},
            ]))
        self.assertEqual([(event.type, event.data.ki.wScan, event.data.ki.dwFlags)
                          for event in self.events[:4]],
                         [(1, 17, 8), (1, 17, 10), (1, 0x4B, 9), (1, 0x4B, 11)])
        self.assertEqual((self.events[4].data.mi.dx, self.events[4].data.mi.dy),
                         (-15, 8))
        self.assertEqual([event.data.mi.dwFlags for event in self.events[4:]], [1, 8, 16])
        self.assertFalse(self.backend.gamepad_available)

    def test_failed_press_still_attempts_release_and_reports_error(self):
        self.user32.SendInput.side_effect = (0, 1)
        with self.assertRaisesRegex(OSError, "SendInput failed"):
            self.backend.execute(game_remote.validate_actions([{"type": "key", "key": "w"}]))
        self.assertEqual(self.user32.SendInput.call_count, 2)
        self.assertEqual(self.backend._held_keys, set())

    def test_optional_gamepad_maps_buttons_axes_triggers_and_resets(self):
        device = Mock()
        enum = SimpleNamespace(**{
            "XUSB_GAMEPAD_" + name: index for index, name in enumerate(
                ("A", "B", "X", "Y", "LEFT_SHOULDER", "RIGHT_SHOULDER", "BACK",
                 "START", "LEFT_THUMB", "RIGHT_THUMB"))
        })
        module = SimpleNamespace(VX360Gamepad=Mock(return_value=device), XUSB_BUTTON=enum)
        with patch.dict("sys.modules", {"vgamepad": module}):
            backend = game_remote.WindowsInput(user32=self.user32, enable_gamepad=True)
        with patch.object(game_remote.time, "sleep"):
            backend.execute(game_remote.validate_actions([
                {"type": "gamepad_button", "button": "south"},
                {"type": "gamepad_stick", "stick": "left", "x": 120, "y": -32768},
                {"type": "gamepad_trigger", "side": "right", "value": 255},
            ]))
        self.assertTrue(backend.gamepad_available)
        device.press_button.assert_called_once_with(button=0)
        device.release_button.assert_called_once_with(button=0)
        self.assertEqual(device.left_joystick.call_args_list[0].kwargs,
                         {"x_value": 120, "y_value": 32767})
        self.assertEqual(device.left_joystick.call_args_list[-1].kwargs,
                         {"x_value": 0, "y_value": 0})
        self.assertEqual([call.kwargs["value"] for call in device.right_trigger.call_args_list],
                         [255, 0])
        device.reset.assert_called_once()
        backend.close()
        self.assertFalse(backend.gamepad_available)

    def test_missing_optional_gamepad_is_an_explicit_error(self):
        with self.assertRaisesRegex(OSError, "not enabled"):
            self.backend.execute(game_remote.validate_actions([
                {"type": "gamepad_button", "button": "south"},
            ]))


class BackendSelectionTests(unittest.TestCase):
    def test_windows_does_not_require_linux_tools(self):
        with patch.object(game_remote.sys, "platform", "win32"), \
                patch.object(game_remote, "ScreenshotCapture") as capture, \
                patch.object(game_remote, "WindowsInput") as backend:
            self.assertEqual(game_remote.create_backends("auto", True),
                             (capture.return_value, backend.return_value))
            capture.assert_called_once_with("windows")
            backend.assert_called_once_with(enable_gamepad=True)

    def test_linux_keeps_its_existing_default(self):
        with patch.object(game_remote.sys, "platform", "linux"), \
                patch.object(game_remote, "ScreenshotCapture") as capture, \
                patch.object(game_remote.shutil, "which", return_value="/bin/ydotool"), \
                patch.object(game_remote, "LinuxInput") as backend:
            game_remote.create_backends("auto")
            capture.assert_called_once_with("grim")
            backend.assert_called_once_with("/bin/ydotool")
            with self.assertRaisesRegex(ValueError, "require Windows"):
                game_remote.create_backends("windows")

    def test_windows_capture_uses_fixed_script_and_checks_png(self):
        image = b"\x89PNG\r\n\x1a\nmock"

        def capture(command, **kwargs):
            Path(kwargs["env"]["THEYS_GONE_CAPTURE"]).write_bytes(image)

        with patch.object(game_remote.shutil, "which", return_value="powershell.exe"), \
                patch.object(game_remote.subprocess, "run", side_effect=capture) as run:
            self.assertEqual(game_remote.ScreenshotCapture("windows").capture(), image)
        self.assertEqual(run.call_args.args[0],
                         ("powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                          game_remote.WINDOWS_CAPTURE_SCRIPT))
        self.assertTrue(run.call_args.kwargs["check"])

    def test_kde_and_xwayland_capture_commands(self):
        image = b"\x89PNG\r\n\x1a\nmock"
        for tool, prefix in (
            ("spectacle", ("/bin/capture", "--background", "--nonotify", "--fullscreen", "--pointer",
                           "--output")),
            ("ffmpeg-x11", ("/bin/capture", "-hide_banner", "-loglevel", "error", "-f",
                            "x11grab", "-i", ":1", "-frames:v", "1", "-y")),
        ):
            def capture(command, **kwargs):
                Path(command[-1]).write_bytes(image)

            with self.subTest(tool=tool), \
                    patch.dict(os.environ, {"DISPLAY": ":1"}), \
                    patch.object(game_remote.shutil, "which", return_value="/bin/capture"), \
                    patch.object(game_remote.subprocess, "run", side_effect=capture) as run:
                self.assertEqual(game_remote.ScreenshotCapture(tool).capture(), image)
                self.assertEqual(run.call_args.args[0][:-1], prefix)

    def test_xwayland_capture_requires_display_and_surfaces_tool_errors(self):
        with patch.object(game_remote.shutil, "which", return_value="/bin/ffmpeg"):
            capture = game_remote.ScreenshotCapture("ffmpeg-x11")
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "requires DISPLAY"):
                capture.capture()
        with patch.dict(os.environ, {"DISPLAY": ":1"}), \
                patch.object(game_remote.subprocess, "run", side_effect=
                             subprocess.CalledProcessError(1, "ffmpeg", stderr=b"cannot open display")):
            with self.assertRaisesRegex(OSError, "cannot open display"):
                capture.capture()

    def test_gamescope_waits_for_complete_png(self):
        prefix = b"\x89PNG\r\n\x1a\nmock"
        end = b"\x00\x00\x00\x00IEND\xaeB`\x82"
        images = []

        def capture(command, **kwargs):
            images.append(Path(command[-1]))
            images[0].write_bytes(prefix)

        def finish(_):
            images[0].write_bytes(prefix + end)

        with patch.dict(os.environ, {"WAYLAND_DISPLAY": "gamescope-0"}), \
                patch.object(game_remote.shutil, "which", return_value="/bin/gamescopectl"), \
                patch.object(game_remote.subprocess, "run", side_effect=capture) as run, \
                patch.object(game_remote.time, "sleep", side_effect=finish):
            self.assertEqual(game_remote.ScreenshotCapture("gamescope").capture(), prefix + end)
        self.assertEqual(run.call_args.args[0][:-1], ("/bin/gamescopectl", "screenshot"))

    def test_gamescope_does_not_report_success_without_an_image(self):
        with patch.dict(os.environ, {"WAYLAND_DISPLAY": "gamescope-0"}), \
                patch.object(game_remote.shutil, "which", return_value="/bin/gamescopectl"), \
                patch.object(game_remote.subprocess, "run"), \
                patch.object(game_remote.time, "monotonic", side_effect=(0, 9)):
            with self.assertRaisesRegex(TimeoutError, "did not finish"):
                game_remote.ScreenshotCapture("gamescope").capture()


class HttpServerTests(unittest.TestCase):
    def setUp(self):
        self.image = b"\x89PNG\r\n\x1a\nmock-png"

        class Capture:
            tool = "grim"

            def capture(inner_self):
                return self.image

        class Input:
            name = "ydotool"

            def __init__(inner_self):
                inner_self.received = None

            gamepad_available = False
            gamepad_error = "uinput is unavailable"

            def execute(inner_self, actions):
                inner_self.received = actions

        self.input = Input()
        self.server = game_remote.make_server(
            "127.0.0.1", 0, "t" * 48, Capture(), self.input)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.addCleanup(self.stop_server)

    def stop_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request(self, method, path, token="t" * 48, body=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port)
        encoded = json.dumps(body).encode() if body is not None else None
        connection.request(method, path, body=encoded, headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        })
        response = connection.getresponse()
        payload = response.read()
        status = response.status
        content_type = response.getheader("Content-Type")
        connection.close()
        return status, content_type, payload

    def test_authentication_health_and_png_capture(self):
        self.assertEqual(self.request("GET", "/v1/health", token="wrong")[0], 401)
        status, content_type, body = self.request("GET", "/v1/health")
        self.assertEqual((status, content_type), (200, "application/json"))
        self.assertEqual(json.loads(body)["input"], "ydotool")
        self.assertFalse(json.loads(body)["gamepad"])
        status, content_type, body = self.request("GET", "/v1/screenshot")
        self.assertEqual((status, content_type, body), (200, "image/png", self.image))

    def test_action_endpoint_validates_before_sending_input(self):
        status, _, body = self.request("POST", "/v1/actions", body=[
            {"type": "key", "key": "w", "duration_ms": 100},
        ])
        self.assertEqual(status, 200, body)
        self.assertEqual(self.input.received[0]["key"], "w")
        status, _, body = self.request("POST", "/v1/actions", body=[
            {"type": "shell", "command": "id"},
        ])
        self.assertEqual(status, 400, body)
        self.assertEqual(self.input.received[0]["key"], "w")


if __name__ == "__main__":
    unittest.main()
