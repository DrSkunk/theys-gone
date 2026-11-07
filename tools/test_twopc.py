import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("vkey", Path(__file__).parent / "twopc" / "vkey.py")
vkey = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vkey)


class ParseStepsTests(unittest.TestCase):
    def test_keys_holds_and_sleeps(self):
        self.assertEqual(vkey.parse_steps(["down", "Enter", "esc:300", "sleep:2000", "f9", "x"]), [
            ("key", "DOWN", vkey.DEFAULT_HOLD_MS), ("key", "ENTER", vkey.DEFAULT_HOLD_MS),
            ("key", "ESC", 300), ("sleep", "", 2000), ("key", "F9", vkey.DEFAULT_HOLD_MS),
            ("key", "X", vkey.DEFAULT_HOLD_MS)])

    def test_rejects_unknown_keys_and_bad_durations(self):
        for bad in (["capslock"], ["f13"], ["sleep"], ["sleep:0"], ["sleep:x"], ["enter:"],
                    ["enter:0"], [f"enter:{vkey.MAX_HOLD_MS + 1}"], [f"sleep:{vkey.MAX_SLEEP_MS + 1}"], []):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                vkey.parse_steps(bad)

    def test_limits_the_number_of_steps(self):
        vkey.parse_steps(["down"] * vkey.MAX_STEPS)
        with self.assertRaises(ValueError):
            vkey.parse_steps(["down"] * (vkey.MAX_STEPS + 1))

    def test_main_reports_bad_input_without_touching_uinput(self):
        self.assertEqual(vkey.main(["nonsense-key"]), 2)


if __name__ == "__main__":
    unittest.main()
