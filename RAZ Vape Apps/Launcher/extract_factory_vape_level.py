#!/usr/bin/env python3
"""Decode and save the factory Raz remaining-vape record.

The factory MyBlueRAZ firmware stores its live heater-time counter in external
SPI flash, not in its 64 KiB MCU program backup. Its record is four
little-endian centisecond ticks plus a 0xBB validity marker at 0x003FF000.

Usage:
    python extract_factory_vape_level.py ..\\..\\firmware\\MyBlueRAZ_backup.bin
    python extract_factory_vape_level.py external_flash_dump.bin --write-json factory_vape_level.json
    python extract_factory_vape_level.py --openocd-log reader.log --write-json factory_vape_level.json
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


MCU_DUMP_SIZE = 0x10000
FACTORY_COUNTER_OFFSET = 0x003F_F000
FACTORY_RECORD_SIZE = 5
FACTORY_VALID_MARKER = 0xBB
FACTORY_SEGMENT_TICKS = 60_000
FACTORY_EMPTY_TICKS = 340_000


def metrics(ticks: int) -> tuple[int, int, int]:
    """Return clamped ticks, factory percentage, and factory six-bar level."""
    used = min(ticks, FACTORY_EMPTY_TICKS)
    bars = 0 if used >= FACTORY_EMPTY_TICKS else 6 - used // FACTORY_SEGMENT_TICKS
    percent = ((FACTORY_EMPTY_TICKS - used) * 100) // FACTORY_EMPTY_TICKS
    return used, percent, bars


def describe_counter(ticks: int) -> None:
    """Print the exact scale recovered from MyBlueRAZ_backup.bin."""
    used, percent, bars = metrics(ticks)
    used_seconds = used / 100.0

    print(f"Factory counter : {ticks} centisecond ticks")
    if ticks > FACTORY_EMPTY_TICKS:
        print("Counter exceeds the factory empty threshold; treating it as empty.")
    print(f"Used heater time: {used_seconds / 60.0:.2f} minutes ({used_seconds:.2f} seconds)")
    print(f"Remaining       : {percent}% ({bars}/6 segments)")


def write_seed(path: Path, ticks: int, source: str) -> None:
    used, percent, bars = metrics(ticks)
    payload = {
        "format": 1,
        "factory_vape_ticks": used,
        "factory_vape_percent": percent,
        "factory_vape_bars": bars,
        "tick_duration_ms": 10,
        "factory_empty_ticks": FACTORY_EMPTY_TICKS,
        "source": source,
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Saved launcher seed: {path} ({percent}%, {bars}/6 segments).")


def inspect_mcu_backup(path: Path, data: bytes) -> None:
    print(f"{path.name} is a {len(data):,}-byte MCU program backup.")
    print("It contains the factory code, but not the live remaining-vape value.")
    print()
    print("MyBlueRAZ reads the live record from external SPI flash:")
    print(f"  address  : 0x{FACTORY_COUNTER_OFFSET:06X}")
    print("  bytes 0-3: little-endian heater-time counter (0.01-second ticks)")
    print(f"  byte 4   : 0x{FACTORY_VALID_MARKER:02X} validity marker")
    print()
    print("Dump that external flash chip, then pass its raw dump to this script")
    print("to recover the original percentage and six-segment level.")


def inspect_external_dump(path: Path, data: bytes, offset: int) -> int:
    if len(data) < offset + FACTORY_RECORD_SIZE:
        raise SystemExit(
            f"{path} is {len(data):,} bytes; it does not include 0x{offset + FACTORY_RECORD_SIZE - 1:06X}."
        )

    record = data[offset : offset + FACTORY_RECORD_SIZE]
    marker = record[4]
    if marker != FACTORY_VALID_MARKER:
        print(f"Record at 0x{offset:06X} is not factory-valid (marker 0x{marker:02X}, expected 0xBB).")
        print("The original firmware treats this as a fresh / zero-use counter.")
        describe_counter(0)
        return 0

    ticks = int.from_bytes(record[:4], "little")
    print(f"Read valid factory record from {path.name} at 0x{offset:06X}.")
    describe_counter(ticks)
    return ticks


def ticks_from_openocd_log(path: Path) -> int:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"^FACTORY_VAPE_TICKS=(\d+)\s*$", text, flags=re.MULTILINE)
    if not match:
        raise SystemExit(
            f"{path} does not contain a valid FACTORY_VAPE_TICKS line; no seed file was written."
        )
    return int(match.group(1))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("dump", nargs="?", type=Path, help="64 KiB MCU backup or raw external-flash dump")
    source.add_argument("--openocd-log", type=Path, help="output captured from read_factory_vape_level.bat")
    parser.add_argument(
        "--external-offset",
        type=lambda value: int(value, 0),
        default=FACTORY_COUNTER_OFFSET,
        help="counter offset in an external dump (default: 0x3FF000)",
    )
    parser.add_argument("--write-json", type=Path, help="save a Launcher import seed as JSON")
    args = parser.parse_args()

    ticks: int | None = None
    source_name: str | None = None
    if args.openocd_log is not None:
        ticks = ticks_from_openocd_log(args.openocd_log)
        source_name = "Factory external SPI flash via read_factory_vape_level.bat"
        describe_counter(ticks)
    else:
        data = args.dump.read_bytes()
        if len(data) == MCU_DUMP_SIZE:
            inspect_mcu_backup(args.dump, data)
        else:
            ticks = inspect_external_dump(args.dump, data, args.external_offset)
            source_name = f"External flash dump: {args.dump.name}"

    if args.write_json is not None:
        if ticks is None or source_name is None:
            raise SystemExit("The MCU program backup has no live counter to save as a Launcher seed.")
        write_seed(args.write_json, ticks, source_name)


if __name__ == "__main__":
    main()
