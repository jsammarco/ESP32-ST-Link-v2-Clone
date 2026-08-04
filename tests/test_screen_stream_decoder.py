from __future__ import annotations

import sys
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from raz_screen_streamer import CommandStreamDecoder  # noqa: E402


def framed(command: bytes) -> bytes:
    length = len(command)
    inverse = length ^ 0xFFFF
    return (
        CommandStreamDecoder.RECORD_MAGIC
        + length.to_bytes(2, "little")
        + inverse.to_bytes(2, "little")
        + command
    )


class ScreenStreamDecoderTests(unittest.TestCase):
    def test_framed_commands_survive_arbitrary_packet_splits(self) -> None:
        decoder = CommandStreamDecoder(8, 8)
        fill = bytes((1, 1, 2, 3, 2, 0x1F, 0x00))
        raw = bytes((2, 0, 0, 2, 1, 0x00, 0xF8, 0xE0, 0x07))
        stream = framed(fill) + framed(raw)
        applied = 0
        for size in (1, 2, 4, 3, 7, 1, 5, 99):
            if not stream:
                break
            applied += decoder.feed(stream[:size])
            stream = stream[size:]
        self.assertEqual(applied, 2)
        self.assertTrue(decoder.framed)
        self.assertEqual(decoder.framebuffer[0:3], decoder.color(0xF800))
        offset = (2 * 8 + 1) * 3
        self.assertEqual(decoder.framebuffer[offset : offset + 3], decoder.color(0x001F))

    def test_framed_decoder_recovers_at_next_record(self) -> None:
        decoder = CommandStreamDecoder(8, 8)
        damaged = framed(bytes((0x99, 0, 0, 1, 1, 0, 0)))
        valid = framed(bytes((1, 4, 4, 1, 1, 0xE0, 0x07)))
        applied = decoder.feed(b"noise" + damaged + valid)
        self.assertEqual(applied, 1)
        self.assertGreaterEqual(decoder.recovered_records, 2)
        offset = (4 * 8 + 4) * 3
        self.assertEqual(decoder.framebuffer[offset : offset + 3], decoder.color(0x07E0))

    def test_legacy_command_stream_still_decodes(self) -> None:
        decoder = CommandStreamDecoder(8, 8)
        self.assertEqual(decoder.feed(bytes((1, 2, 3, 1, 1, 0x1F, 0x00))), 1)
        self.assertFalse(decoder.framed)
        offset = (3 * 8 + 2) * 3
        self.assertEqual(decoder.framebuffer[offset : offset + 3], decoder.color(0x001F))


if __name__ == "__main__":
    unittest.main()
