#!/usr/bin/env python3
"""Expose the ESP32 USB-serial SWD bridge as OpenOCD remote_bitbang TCP.

The ESP32 DevKit V1's CP2102/CH340 interface appears as a COM port.  OpenOCD's
remote_bitbang driver expects TCP rather than a serial device, so this program
transparently forwards bytes between COMx and 127.0.0.1:3335. No Wi-Fi or LAN
connection is involved.
"""

from __future__ import annotations

import argparse
import socket
import threading
import time

try:
    import serial
except ModuleNotFoundError as exc:
    raise SystemExit("Missing dependency. Install it with: python -m pip install pyserial") from exc


BAUD = 230400
HOST = "127.0.0.1"
PORT = 3335


def forward_serial_to_tcp(device: serial.Serial, client: socket.socket,
                          stop: threading.Event) -> None:
    """Forward each ESP32 sample immediately, then drain a short burst.

    OpenOCD requests SWD input one bit at a time. A read(256) waits until its
    timeout after receiving a single bit, adding 50 ms to every SWD sample and
    making a simple probe take minutes. Reading the first byte separately
    returns as soon as it arrives; in_waiting then drains any queued samples.
    """
    while not stop.is_set():
        try:
            first_byte = device.read(1)
            if not first_byte:
                continue
            queued = min(device.in_waiting, 255)
            data = first_byte + (device.read(queued) if queued else b"")
            client.sendall(data)
        except (OSError, serial.SerialException):
            stop.set()


def proxy_one_client(device: serial.Serial, client: socket.socket) -> None:
    stop = threading.Event()
    reader = threading.Thread(
        target=forward_serial_to_tcp,
        args=(device, client, stop),
        daemon=True,
    )
    reader.start()
    try:
        while not stop.is_set():
            try:
                data = client.recv(1024)
            except socket.timeout:
                continue
            if not data:
                break
            device.write(data)
            # Keep the request timing deterministic, especially before an
            # OpenOCD read that waits for a single SWDIO sample.
            device.flush()
    except OSError:
        pass
    finally:
        stop.set()
        try:
            client.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        client.close()
        reader.join(timeout=0.2)
        # Drop any late byte left by a closed OpenOCD session before accepting
        # another one. The next session always sends a fresh SWD line reset.
        device.reset_input_buffer()


def select_swd_pin_map(device: serial.Serial) -> str:
    """Ask current ESP32 firmware to probe its two SWD pin assignments.

    The direct probe leaves the successful mapping selected. Running this just
    before OpenOCD connects gives the optional remote_bitbang path the same
    automatic GPIO26/GPIO25 then GPIO25/GPIO26 fallback as fast_flash.py.
    It never halts or writes the target.
    """
    device.reset_input_buffer()
    device.write(b"I")
    device.flush()
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        response = device.readline()
        if response:
            device.reset_input_buffer()
            return response.decode("ascii", errors="replace").strip()
    device.reset_input_buffer()
    return "no response"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="ESP32 serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=BAUD, help=f"serial rate (default: {BAUD})")
    parser.add_argument(
        "--listen-host",
        default=HOST,
        help=f"TCP listen address (default: {HOST}; use 0.0.0.0 for WSL2 if needed)",
    )
    parser.add_argument("--tcp-port", type=int, default=PORT, help=f"localhost TCP port (default: {PORT})")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    with serial.Serial(
        args.port,
        args.baud,
        timeout=0.05,
        write_timeout=2,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as device:
        # CP2102/CH340 DevKit boards commonly connect DTR/RTS to EN/GPIO0.
        # Opening the COM port can reset the ESP32; use normal inactive levels,
        # wait out the boot banner, then remove it from the protocol stream.
        device.dtr = False
        device.rts = False
        time.sleep(1.25)
        device.reset_input_buffer()
        device.reset_output_buffer()

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listener.bind((args.listen_host, args.tcp_port))
            listener.listen(1)
            # On Windows, a blocking accept() can defer Ctrl+C handling until
            # another connection arrives. Polling lets KeyboardInterrupt stop
            # an idle bridge promptly and release the COM port.
            listener.settimeout(0.25)
            print(f"ESP32 serial SWD bridge: {args.port} -> tcp://{args.listen_host}:{args.tcp_port}")
            print("Leave this window open, then run OpenOCD in another window. Ctrl+C stops it.")
            while True:
                try:
                    client, address = listener.accept()
                except socket.timeout:
                    continue
                print(f"OpenOCD connected from {address[0]}:{address[1]}")
                client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                client.settimeout(0.25)
                pin_map_probe = select_swd_pin_map(device)
                if pin_map_probe.startswith("IDR "):
                    print("ESP32 automatic SWD pin-map probe succeeded.")
                else:
                    print(f"ESP32 automatic SWD pin-map probe: {pin_map_probe}")
                proxy_one_client(device, client)
                print("OpenOCD disconnected")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nSerial bridge stopped")
