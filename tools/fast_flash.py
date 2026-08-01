#!/usr/bin/env python3
"""Program the N32G031 locally from the ESP32, without OpenOCD bit round trips.

Use only after uploading the firmware in src/main.cpp that implements the
direct probe, flash, backup, and restore protocol. The normal
serial_bridge.py process must be stopped because this tool opens the ESP32 COM
port directly.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
except ModuleNotFoundError as exc:
    raise SystemExit("Missing dependency. Install it with: python -m pip install pyserial") from exc


BAUD = 230400
MAX_APP_IMAGE_BYTES = 60 * 1024
FULL_FLASH_BYTES = 64 * 1024
TARGET_RAM_BYTES = 8 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 serial port, e.g. COM7")
    parser.add_argument("--baud", type=int, default=BAUD)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--probe", action="store_true", help="Read the target DPIDR only; writes nothing")
    mode.add_argument("--flash", type=Path, metavar="IMAGE", help="Program and verify this .bin image")
    mode.add_argument("--backup", type=Path, metavar="DIRECTORY", help="Save full internal flash and a RAM snapshot")
    mode.add_argument("--restore", type=Path, metavar="BACKUP", help="Restore a full internal-flash backup")
    parser.add_argument(
        "--confirm-restore",
        action="store_true",
        help="Required with --restore because it erases all 64 KB, including persistent settings",
    )
    return parser.parse_args()


def open_esp32(port: str, baud: int) -> serial.Serial:
    device = serial.Serial(
        port,
        baud,
        timeout=0.25,
        write_timeout=30,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    )
    # Opening a DevKit COM port can reset the board through DTR/RTS. Wait for
    # the new firmware, then discard the ROM boot banner before sending a command.
    device.dtr = False
    device.rts = False
    time.sleep(1.25)
    device.reset_input_buffer()
    device.reset_output_buffer()
    return device


def read_line(device: serial.Serial, deadline: float) -> str:
    while time.monotonic() < deadline:
        raw = device.readline()
        if raw:
            return raw.decode("ascii", errors="replace").strip()
    raise TimeoutError("Timed out waiting for the ESP32.")


def read_exact(device: serial.Serial, size: int, deadline: float) -> bytes:
    """Read an exact binary payload without interpreting its contents as text."""
    data = bytearray()
    while len(data) < size:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"Timed out after receiving {len(data):,} of {size:,} bytes.")
        chunk = device.read(min(4096, size - len(data)))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def probe(device: serial.Serial) -> int:
    device.write(b"I")
    device.flush()
    response = read_line(device, time.monotonic() + 10)
    print(response)
    if not response.startswith("IDR "):
        raise RuntimeError(f"ESP32 SWD probe failed: {response}")
    return 0


def flash(device: serial.Serial, image_path: Path) -> int:
    if not image_path.is_file():
        raise RuntimeError(f"Image not found: {image_path}")
    image = image_path.read_bytes()
    if not image:
        raise RuntimeError("Refusing to flash an empty image.")
    image += b"\xff" * ((-len(image)) % 4)
    if len(image) > MAX_APP_IMAGE_BYTES:
        raise RuntimeError(
            f"Image is {len(image):,} bytes; the safe application region is {MAX_APP_IMAGE_BYTES:,} bytes."
        )

    print(f"Sending {len(image):,} bytes to the ESP32...")
    device.write(b"F" + struct.pack("<I", len(image)))
    device.flush()
    response = read_line(device, time.monotonic() + 5)
    if response != "READY":
        raise RuntimeError(f"ESP32 did not accept the image: {response}")

    device.write(image)
    device.flush()
    print("ESP32 is erasing, programming, and verifying locally:")

    deadline = time.monotonic() + 180
    while True:
        response = read_line(device, deadline)
        print(response)
        if response == "DONE":
            return 0
        if response.startswith("ERR") or response.startswith("VERIFY_FAIL"):
            raise RuntimeError(f"ESP32 fast flash failed: {response}")


def backup(device: serial.Serial, output_dir: Path) -> int:
    if output_dir.exists():
        raise RuntimeError(f"Backup destination already exists: {output_dir}")

    device.write(b"K")
    device.flush()
    header = read_line(device, time.monotonic() + 15)
    fields = header.split()
    if len(fields) != 4 or fields[0] != "BACKUP":
        raise RuntimeError(f"ESP32 did not start a backup: {header}")
    try:
        flash_size, ram_size = int(fields[1]), int(fields[2])
        dpidr = int(fields[3], 16)
    except ValueError as exc:
        raise RuntimeError(f"Invalid ESP32 backup header: {header}") from exc
    if flash_size != FULL_FLASH_BYTES or ram_size != TARGET_RAM_BYTES:
        raise RuntimeError(f"Unexpected target memory layout: {header}")

    print(f"Reading {flash_size:,} bytes of internal flash and {ram_size:,} bytes of RAM...")
    device.write(b"C")
    device.flush()
    deadline = time.monotonic() + 180
    internal_flash = read_exact(device, flash_size, deadline)
    ram = read_exact(device, ram_size, deadline)
    response = read_line(device, deadline)
    if response != "DONE":
        raise RuntimeError(f"ESP32 backup did not complete: {response}")

    output_dir.mkdir(parents=True)
    flash_path = output_dir / "internal_flash.bin"
    ram_path = output_dir / "ram_snapshot.bin"
    flash_path.write_bytes(internal_flash)
    ram_path.write_bytes(ram)
    manifest = {
        "format": "esp32-raz-backup-v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "dpidr": f"0x{dpidr:08X}",
        "internal_flash": {
            "file": flash_path.name,
            "bytes": len(internal_flash),
            "sha256": hashlib.sha256(internal_flash).hexdigest(),
        },
        "ram_snapshot": {
            "file": ram_path.name,
            "bytes": len(ram),
            "sha256": hashlib.sha256(ram).hexdigest(),
            "note": "Volatile live snapshot; it is not restored after a reset.",
        },
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Backup saved to: {output_dir}")
    print(f"Flash SHA-256: {manifest['internal_flash']['sha256']}")
    return 0


def load_restore_image(backup: Path) -> bytes:
    if backup.is_dir():
        image_path = backup / "internal_flash.bin"
        manifest_path = backup / "manifest.json"
        if manifest_path.is_file():
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            expected = manifest.get("internal_flash", {}).get("sha256")
            if expected and image_path.is_file():
                actual = hashlib.sha256(image_path.read_bytes()).hexdigest()
                if actual.lower() != expected.lower():
                    raise RuntimeError("Backup SHA-256 does not match manifest.json; refusing to restore.")
    else:
        image_path = backup

    if not image_path.is_file():
        raise RuntimeError(f"Full-flash backup image not found: {image_path}")
    image = image_path.read_bytes()
    if len(image) != FULL_FLASH_BYTES:
        raise RuntimeError(
            f"Restore requires an exact {FULL_FLASH_BYTES:,}-byte internal_flash.bin; got {len(image):,} bytes."
        )
    return image


def restore(device: serial.Serial, backup_path: Path) -> int:
    image = load_restore_image(backup_path)
    print(f"Restoring {len(image):,} bytes of internal flash from: {backup_path}")
    print("This overwrites the application and its persistent NV/settings region.")
    device.write(b"X" + struct.pack("<I", len(image)))
    device.flush()
    response = read_line(device, time.monotonic() + 5)
    if response != "RESTORE_READY":
        raise RuntimeError(f"ESP32 did not accept the restore image: {response}")
    device.write(image)
    device.flush()
    print("ESP32 is erasing, restoring, and verifying locally:")

    deadline = time.monotonic() + 300
    while True:
        response = read_line(device, deadline)
        print(response)
        if response == "DONE":
            return 0
        if response.startswith("ERR") or response.startswith("VERIFY_FAIL"):
            raise RuntimeError(f"ESP32 restore failed: {response}")


def main() -> int:
    args = parse_args()
    with open_esp32(args.port, args.baud) as device:
        if args.probe:
            return probe(device)
        if args.flash:
            return flash(device, args.flash)
        if args.backup:
            return backup(device, args.backup)
        if not args.confirm_restore:
            raise RuntimeError("--restore requires --confirm-restore because it erases all internal flash.")
        return restore(device, args.restore)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, serial.SerialException, RuntimeError, TimeoutError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
