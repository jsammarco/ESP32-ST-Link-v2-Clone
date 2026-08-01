#!/usr/bin/env python3
"""Program the N32G031 locally from the ESP32, without OpenOCD bit round trips.

Use only after uploading the firmware in src/main.cpp that implements the
``I`` probe and ``F`` fast-flash protocol. The normal serial_bridge.py process
must be stopped because this tool opens the ESP32 COM port directly.
"""

from __future__ import annotations

import argparse
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ModuleNotFoundError as exc:
    raise SystemExit("Missing dependency. Install it with: python -m pip install pyserial") from exc


BAUD = 230400
MAX_IMAGE_BYTES = 60 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 serial port, e.g. COM7")
    parser.add_argument("--baud", type=int, default=BAUD)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--probe", action="store_true", help="Read the target DPIDR only; writes nothing")
    mode.add_argument("--flash", type=Path, metavar="IMAGE", help="Program and verify this .bin image")
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
    if len(image) > MAX_IMAGE_BYTES:
        raise RuntimeError(
            f"Image is {len(image):,} bytes; the safe application region is {MAX_IMAGE_BYTES:,} bytes."
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


def main() -> int:
    args = parse_args()
    with open_esp32(args.port, args.baud) as device:
        if args.probe:
            return probe(device)
        return flash(device, args.flash)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, serial.SerialException, RuntimeError, TimeoutError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
