from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest.mock import patch


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import fast_flash  # noqa: E402


class FakeSerial:
    def __init__(self, replies: list[str]) -> None:
        self.replies = iter(replies)
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> None:
        self.writes.append(data)

    def flush(self) -> None:
        pass

    def readline(self) -> bytes:
        return (next(self.replies) + "\n").encode("ascii")


class FastFlashConfigTests(unittest.TestCase):
    @patch("fast_flash.time.sleep", return_value=None)
    def test_launcher_config_retries_transient_connect_failures(self, _sleep: object) -> None:
        device = FakeSerial(
            [
                "ERR CONNECT",
                "ERR CONNECT",
                "IDR 0BC11477",
                "CONFIG_OK 7 43504F00",
                "DONE",
            ]
        )
        result = fast_flash.set_coil_profile(device, "default")  # type: ignore[arg-type]
        self.assertEqual(result, 0)
        self.assertEqual(len(device.writes), 3)
        self.assertTrue(all(command.startswith(b"N") for command in device.writes))


if __name__ == "__main__":
    unittest.main()
