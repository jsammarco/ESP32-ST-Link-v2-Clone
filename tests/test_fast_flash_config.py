from __future__ import annotations

import sys
import tempfile
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
    def test_normalize_protocol_line_removes_reset_noise_before_mode(self) -> None:
        noisy = ("\ufffd" * 64) + "MODE PROGRAMMER MAP2 IDLE=HIGH-Z"
        self.assertEqual(
            fast_flash.normalize_protocol_line(noisy),
            "MODE PROGRAMMER MAP2 IDLE=HIGH-Z",
        )

    def test_normalize_protocol_line_preserves_real_prefix(self) -> None:
        line = "unexpected MODE PROGRAMMER MAP2 IDLE=HIGH-Z"
        self.assertEqual(fast_flash.normalize_protocol_line(line), line)

    def test_backup_connect_failure_explains_physical_reset_requirement(self) -> None:
        device = FakeSerial(["ERR CONNECT"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "backup"
            with self.assertRaisesRegex(RuntimeError, "actually resetting or power-cycling"):
                fast_flash.backup(device, output)  # type: ignore[arg-type]
        self.assertEqual(device.writes, [b"K"])

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

    @patch("fast_flash.time.sleep", return_value=None)
    def test_runtime_diagnostics_reads_complete_report(self, _sleep: object) -> None:
        device = FakeSerial(
            [
                "DIAG BEGIN",
                "DIAG MODE=RUNTIME MAP=1 RX=GPIO25 TX=GPIO26 TX_ATTACHED=1 TX_ATTACH_FAILED=0",
                "DIAG UART RX_BYTES=30 RX_LINES=5 PINGS=4 TX_LINES=8 PARTIAL=0",
                "DIAG WIFI SDK_STATUS=6 LINK=DISCONNECTED SCAN_ACTIVE=0 SCAN_STARTS=1 SCAN_DONE=1 LAST_SCAN=4 APS=4",
                "DIAG END",
            ]
        )
        device.reset_input_buffer = lambda: None  # type: ignore[attr-defined]
        result = fast_flash.esp32_diagnostics(device)  # type: ignore[arg-type]
        self.assertEqual(result, 0)
        self.assertEqual(device.writes, [b"D"])


if __name__ == "__main__":
    unittest.main()
