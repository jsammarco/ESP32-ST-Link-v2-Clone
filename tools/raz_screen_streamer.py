#!/usr/bin/env python3
"""Live RAZ screen viewer, PNG snapshot tool, and AVI recorder."""

from __future__ import annotations

import argparse
import queue
import struct
import threading
import time
import zlib
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

try:
    import serial
    from serial.tools import list_ports
except ModuleNotFoundError as exc:
    raise SystemExit("Missing dependency. Install it with: python -m pip install pyserial") from exc


BAUD = 230400
PACKET_MAGIC = b"RZF1"
ERROR_MAGIC = b"RZE1"
COMMAND_PACKET_MAGIC = b"RZC2"
COMMAND_ERROR_MAGIC = b"RZE2"
OUTPUT_WIDTH = 128
OUTPUT_HEIGHT = 160
RECORDING_FPS = 5


def rgb121_palette() -> tuple[bytes, ...]:
    colors: list[bytes] = []
    for value in range(16):
        red = 255 if value & 0x8 else 0
        green = ((value >> 1) & 0x3) * 85
        blue = 255 if value & 0x1 else 0
        colors.append(bytes((red, green, blue)))
    return tuple(colors)


PALETTE = rgb121_palette()


def bgr565_to_rgb(color: int) -> bytes:
    red = (color & 0x1F) * 255 // 31
    green = ((color >> 5) & 0x3F) * 255 // 63
    blue = ((color >> 11) & 0x1F) * 255 // 31
    return bytes((red, green, blue))


class CommandStreamDecoder:
    """Reconstruct a native RGB screen from target display commands."""

    OP_FILL = 1
    OP_RAW = 2
    OP_RLE = 3

    def __init__(self, width: int, height: int) -> None:
        self.width = width
        self.height = height
        self.framebuffer = bytearray(width * height * 3)
        self.pending = bytearray()
        self.color_cache: dict[int, bytes] = {}

    def color(self, value: int) -> bytes:
        result = self.color_cache.get(value)
        if result is None:
            result = bgr565_to_rgb(value)
            self.color_cache[value] = result
        return result

    def set_pixel(self, x: int, y: int, color: bytes) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            offset = (y * self.width + x) * 3
            self.framebuffer[offset : offset + 3] = color

    def fill(self, x: int, y: int, width: int, height: int, color: bytes) -> None:
        left = max(0, x)
        top = max(0, y)
        right = min(self.width, x + width)
        bottom = min(self.height, y + height)
        if left >= right or top >= bottom:
            return
        row = color * (right - left)
        for output_y in range(top, bottom):
            offset = (output_y * self.width + left) * 3
            self.framebuffer[offset : offset + len(row)] = row

    def apply_raw(self, x: int, y: int, width: int, height: int, pixels: memoryview) -> None:
        source = 0
        for row in range(height):
            for column in range(width):
                value = pixels[source] | (pixels[source + 1] << 8)
                self.set_pixel(x + column, y + row, self.color(value))
                source += 2

    def apply_rle(self, x: int, y: int, width: int, height: int, encoded: memoryview) -> None:
        pixel = 0
        offset = 0
        total = width * height
        while pixel < total:
            run = encoded[offset]
            value = encoded[offset + 1] | (encoded[offset + 2] << 8)
            color = self.color(value)
            for _ in range(run):
                self.set_pixel(x + pixel % width, y + pixel // width, color)
                pixel += 1
            offset += 3

    def feed(self, data: bytes) -> int:
        self.pending.extend(data)
        offset = 0
        applied = 0
        view = bytes(self.pending)
        while len(view) - offset >= 5:
            opcode, x, y, width, height = view[offset : offset + 5]
            if width == 0 or height == 0:
                raise RuntimeError("Invalid zero-sized display command from ESP32.")
            pixels = width * height
            if opcode == self.OP_FILL:
                if len(view) - offset < 7:
                    break
                value = view[offset + 5] | (view[offset + 6] << 8)
                self.fill(x, y, width, height, self.color(value))
                offset += 7
            elif opcode == self.OP_RAW:
                command_bytes = 5 + pixels * 2
                if len(view) - offset < command_bytes:
                    break
                self.apply_raw(x, y, width, height, memoryview(view)[offset + 5 : offset + command_bytes])
                offset += command_bytes
            elif opcode == self.OP_RLE:
                encoded_offset = offset + 5
                decoded = 0
                incomplete = False
                while decoded < pixels:
                    if len(view) - encoded_offset < 3:
                        incomplete = True
                        break
                    run = view[encoded_offset]
                    if run == 0 or decoded + run > pixels:
                        raise RuntimeError("Invalid RLE display command from ESP32.")
                    decoded += run
                    encoded_offset += 3
                if incomplete:
                    break
                self.apply_rle(x, y, width, height, memoryview(view)[offset + 5 : encoded_offset])
                offset = encoded_offset
            else:
                raise RuntimeError(f"Unknown display command 0x{opcode:02X} from ESP32.")
            applied += 1
        if offset:
            del self.pending[:offset]
        return applied


def decode_rgb121(packed: bytes, width: int, height: int) -> bytes:
    pixels = bytearray(width * height * 3)
    output = 0
    for byte in packed:
        pixels[output : output + 3] = PALETTE[byte & 0x0F]
        output += 3
        pixels[output : output + 3] = PALETTE[byte >> 4]
        output += 3
    return bytes(pixels)


def scale_nearest(rgb: bytes, width: int, height: int, factor: int) -> bytes:
    scaled_row_bytes = width * factor * 3
    scaled = bytearray(scaled_row_bytes * height * factor)
    destination = 0
    for y in range(height):
        source_row = rgb[y * width * 3 : (y + 1) * width * 3]
        expanded_row = b"".join(
            source_row[x : x + 3] * factor for x in range(0, len(source_row), 3)
        )
        for _ in range(factor):
            scaled[destination : destination + scaled_row_bytes] = expanded_row
            destination += scaled_row_bytes
    return bytes(scaled)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = zlib.crc32(kind)
    checksum = zlib.crc32(payload, checksum)
    return kind + payload + struct.pack(">I", checksum & 0xFFFFFFFF)


def write_png(path: Path, rgb: bytes, width: int, height: int) -> None:
    stride = width * 3
    scanlines = b"".join(b"\x00" + rgb[row * stride : (row + 1) * stride] for row in range(height))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    compressed = zlib.compress(scanlines, 6)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + struct.pack(">I", len(header))
        + png_chunk(b"IHDR", header)
        + struct.pack(">I", len(compressed))
        + png_chunk(b"IDAT", compressed)
        + struct.pack(">I", 0)
        + png_chunk(b"IEND", b"")
    )


class AviWriter:
    """Small dependency-free uncompressed RGB AVI writer."""

    def __init__(self, path: Path, width: int, height: int, fps: int) -> None:
        self.path = path
        self.width = width
        self.height = height
        self.fps = fps
        self.row_bytes = ((width * 3 + 3) // 4) * 4
        self.frame_bytes = self.row_bytes * height
        self.frames = 0
        self.index: list[tuple[int, int]] = []
        self.file = path.open("w+b")
        self._write_header()

    def _write_header(self) -> None:
        output = self.file
        output.write(b"RIFF\x00\x00\x00\x00AVI ")
        output.write(b"LIST" + struct.pack("<I", 192) + b"hdrl")
        avih = struct.pack(
            "<14I",
            1_000_000 // self.fps,
            self.frame_bytes * self.fps,
            0,
            0x10,
            0,
            0,
            1,
            self.frame_bytes,
            self.width,
            self.height,
            0,
            0,
            0,
            0,
        )
        output.write(b"avih" + struct.pack("<I", len(avih)))
        self.avih_payload = output.tell()
        output.write(avih)
        output.write(b"LIST" + struct.pack("<I", 116) + b"strl")
        strh = struct.pack(
            "<4s4sIHHIIIIIIIIhhhh",
            b"vids",
            b"DIB ",
            0,
            0,
            0,
            0,
            1,
            self.fps,
            0,
            0,
            self.frame_bytes,
            0xFFFFFFFF,
            0,
            0,
            0,
            self.width,
            self.height,
        )
        output.write(b"strh" + struct.pack("<I", len(strh)))
        self.strh_payload = output.tell()
        output.write(strh)
        strf = struct.pack(
            "<IiiHHIIiiII",
            40,
            self.width,
            self.height,
            1,
            24,
            0,
            self.frame_bytes,
            0,
            0,
            0,
            0,
        )
        output.write(b"strf" + struct.pack("<I", len(strf)) + strf)
        output.write(b"LIST")
        self.movi_size_position = output.tell()
        output.write(b"\x00\x00\x00\x00movi")
        self.movi_data_start = output.tell()

    def write(self, rgb: bytes) -> None:
        if len(rgb) != self.width * self.height * 3:
            raise ValueError("AVI frame dimensions do not match the recording.")
        bgr = bytearray(self.frame_bytes)
        destination = 0
        source_stride = self.width * 3
        padding = self.row_bytes - source_stride
        for y in range(self.height - 1, -1, -1):
            row = rgb[y * source_stride : (y + 1) * source_stride]
            for x in range(0, len(row), 3):
                bgr[destination : destination + 3] = bytes((row[x + 2], row[x + 1], row[x]))
                destination += 3
            destination += padding

        chunk_position = self.file.tell()
        self.file.write(b"00db" + struct.pack("<I", len(bgr)) + bgr)
        if len(bgr) & 1:
            self.file.write(b"\x00")
        self.index.append((chunk_position - self.movi_data_start, len(bgr)))
        self.frames += 1

    def close(self) -> None:
        if self.file.closed:
            return
        index_position = self.file.tell()
        index_payload = bytearray()
        for offset, size in self.index:
            index_payload.extend(struct.pack("<4sIII", b"00db", 0x10, offset, size))
        self.file.write(b"idx1" + struct.pack("<I", len(index_payload)) + index_payload)
        end = self.file.tell()
        self.file.seek(4)
        self.file.write(struct.pack("<I", end - 8))
        self.file.seek(self.movi_size_position)
        self.file.write(struct.pack("<I", index_position - (self.movi_size_position + 4)))
        self.file.seek(self.avih_payload + 16)
        self.file.write(struct.pack("<I", self.frames))
        self.file.seek(self.strh_payload + 32)
        self.file.write(struct.pack("<I", self.frames))
        self.file.close()


class ScreenStreamer(tk.Tk):
    def __init__(self, initial_port: str = "") -> None:
        super().__init__()
        self.title("RAZ Screen Streamer")
        self.resizable(False, False)
        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.stop_event = threading.Event()
        self.worker: threading.Thread | None = None
        self.device: serial.Serial | None = None
        self.closing = False
        self.recording: AviWriter | None = None
        self.recording_started = 0.0
        self.latest_rgb: bytes | None = None
        self.latest_photo: tk.PhotoImage | None = None
        self.port_var = tk.StringVar(value=initial_port)
        self.status_var = tk.StringVar(value="Connect the ESP32 to a stream-enabled RAZ app.")
        self.fps_var = tk.StringVar(value="No frames yet")
        self._frame_times: list[float] = []
        self._build_ui()
        self.refresh_ports()
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.after(50, self._drain_events)
        if initial_port:
            self.after(250, self.connect)

    def _build_ui(self) -> None:
        main = ttk.Frame(self, padding=12)
        main.grid()
        ttk.Label(main, text="ESP32 port:").grid(row=0, column=0, sticky="w")
        self.port_box = ttk.Combobox(main, textvariable=self.port_var, width=18, state="normal")
        self.port_box.grid(row=0, column=1, sticky="w", padx=(6, 0))
        self.connect_button = ttk.Button(main, text="Connect", command=self.connect)
        self.connect_button.grid(row=0, column=2, padx=(8, 0))
        ttk.Button(main, text="Refresh", command=self.refresh_ports).grid(row=0, column=3, padx=(6, 0))

        self.screen = ttk.Label(main, anchor="center", relief="sunken")
        self.screen.grid(row=1, column=0, columnspan=4, pady=(12, 10))
        blank = bytes(256 * 320 * 3)
        self._show_preview(blank, 256, 320)

        ttk.Button(main, text="Save snapshot...", command=self.snapshot).grid(row=2, column=0, columnspan=2)
        self.record_button = ttk.Button(main, text="Start AVI recording...", command=self.toggle_recording)
        self.record_button.grid(row=2, column=2, columnspan=2)
        ttk.Label(main, textvariable=self.fps_var).grid(row=3, column=0, columnspan=4, pady=(8, 0))
        ttk.Label(main, textvariable=self.status_var, wraplength=360, justify="left").grid(
            row=4, column=0, columnspan=4, sticky="w", pady=(6, 0)
        )

    def refresh_ports(self) -> None:
        ports = [item.device for item in list_ports.comports() if item.device.upper() != "COM1"]
        self.port_box["values"] = ports
        if ports and not self.port_var.get().strip():
            self.port_var.set(ports[0])

    def connect(self) -> None:
        if self.worker is not None and self.worker.is_alive():
            self.stop_event.set()
            self.connect_button.configure(text="Connect", state="disabled")
            self._cancel_serial_read()
            return
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("ESP32 port required", "Choose or type the ESP32 COM port.", parent=self)
            return
        self.stop_event.clear()
        self.connect_button.configure(text="Disconnect")
        self.status_var.set("Connecting and locating the screen mirror...")
        self.worker = threading.Thread(target=self._stream_worker, args=(port,), daemon=False)
        self.worker.start()

    def _cancel_serial_read(self) -> None:
        device = self.device
        if device is not None:
            try:
                device.cancel_read()
            except (AttributeError, OSError, serial.SerialException):
                pass

    def _read_line(self, device: serial.Serial, deadline: float) -> str:
        while time.monotonic() < deadline and not self.stop_event.is_set():
            line = device.readline()
            if line:
                return line.decode("ascii", errors="replace").strip()
        if self.stop_event.is_set():
            raise RuntimeError("Screen stream disconnected.")
        raise TimeoutError("Timed out waiting for the ESP32 screen-stream header.")

    def _read_exact(self, device: serial.Serial, size: int) -> bytes:
        data = bytearray()
        while len(data) < size and not self.stop_event.is_set():
            chunk = device.read(size - len(data))
            if chunk:
                data.extend(chunk)
        if len(data) != size:
            raise RuntimeError("Screen stream stopped before a complete frame arrived.")
        return bytes(data)

    def _find_packet_magic(self, device: serial.Serial) -> bytes:
        window = bytearray()
        known_magics = (PACKET_MAGIC, ERROR_MAGIC, COMMAND_PACKET_MAGIC, COMMAND_ERROR_MAGIC)
        while not self.stop_event.is_set():
            byte = device.read(1)
            if not byte:
                continue
            window.extend(byte)
            if len(window) > 4:
                del window[0]
            candidate = bytes(window)
            if candidate in known_magics:
                return candidate
        raise RuntimeError("Screen stream disconnected.")

    def _stream_worker(self, port: str) -> None:
        try:
            device = serial.Serial(port, BAUD, timeout=0.25, write_timeout=2)
            self.device = device
            device.dtr = False
            device.rts = False
            if self.stop_event.wait(1.25):
                return
            device.reset_input_buffer()
            device.reset_output_buffer()
            device.write(b"Y")
            device.flush()
            try:
                capability = self._read_line(device, time.monotonic() + 1.5)
            except TimeoutError:
                capability = ""
            native_capable = "STREAM_CMD2" in capability
            if capability and capability.startswith("RAZ_ESP32") and not native_capable:
                self.events.put(("notice", "ESP32 firmware uses the legacy stream protocol."))
            device.reset_input_buffer()
            device.write(b"S")
            device.flush()
            header = self._read_line(device, time.monotonic() + 15)
            fields = header.split()
            if len(fields) != 6 or fields[0] != "STREAM":
                if header == "ERR NO_STREAM_APP":
                    if not native_capable:
                        raise RuntimeError(
                            "Update the ESP32 firmware in RAZ Manager, then reflash the RAZ app "
                            "with the full-resolution screen streamer checked."
                        )
                    raise RuntimeError(
                        'The flashed app has no screen stream. Reflash it with the full-resolution streamer checked.'
                    )
                raise RuntimeError(f"ESP32 could not start screen streaming: {header}")
            width, height, bpp, payload_bytes = map(int, fields[1:5])
            if bpp == 16 and payload_bytes == 0:
                if width != OUTPUT_WIDTH or height != OUTPUT_HEIGHT:
                    raise RuntimeError(f"Unsupported native screen size: {header}")
                decoder = CommandStreamDecoder(width, height)
                sequence = 0
                self.events.put(("connected", f"Streaming native {width}x{height} RGB565 over SWD on {port}."))
                while not self.stop_event.is_set():
                    magic = self._find_packet_magic(device)
                    if magic == COMMAND_ERROR_MAGIC:
                        self._read_exact(device, 4)
                        raise RuntimeError("The ESP32 lost SWD access while reading the live screen.")
                    if magic != COMMAND_PACKET_MAGIC:
                        raise RuntimeError("ESP32 sent a legacy frame during a native screen stream.")
                    command_bytes = struct.unpack("<H", self._read_exact(device, 2))[0]
                    if command_bytes == 0 or command_bytes > 512:
                        raise RuntimeError("ESP32 sent an invalid native screen-stream packet.")
                    applied = decoder.feed(self._read_exact(device, command_bytes))
                    if applied:
                        sequence += 1
                        self.events.put(
                            ("frame", (sequence, width, height, bytes(decoder.framebuffer)))
                        )
            elif bpp == 4 and payload_bytes == width * height // 2:
                self.events.put(("connected", f"Streaming legacy {width}x{height} RGB121 over SWD on {port}."))
                while not self.stop_event.is_set():
                    magic = self._find_packet_magic(device)
                    if magic == ERROR_MAGIC:
                        self._read_exact(device, 4)
                        raise RuntimeError("The ESP32 lost SWD access while reading the live screen.")
                    if magic != PACKET_MAGIC:
                        raise RuntimeError("ESP32 sent a native packet during a legacy screen stream.")
                    sequence = struct.unpack("<I", self._read_exact(device, 4))[0]
                    packed = self._read_exact(device, payload_bytes)
                    rgb = decode_rgb121(packed, width, height)
                    self.events.put(("frame", (sequence, width, height, rgb)))
            else:
                raise RuntimeError(f"Unsupported screen-stream format: {header}")
        except (OSError, RuntimeError, TimeoutError, serial.SerialException) as exc:
            if not self.stop_event.is_set():
                self.events.put(("error", str(exc)))
        finally:
            device = self.device
            self.device = None
            if device is not None:
                try:
                    if device.is_open:
                        device.write(b"Q")
                except (OSError, serial.SerialException):
                    pass
                try:
                    device.close()
                except (OSError, serial.SerialException):
                    pass
            self.events.put(("disconnected", None))

    def _show_preview(self, rgb: bytes, width: int, height: int) -> None:
        ppm = f"P6\n{width} {height}\n255\n".encode("ascii") + rgb
        self.latest_photo = tk.PhotoImage(data=ppm, format="PPM")
        self.screen.configure(image=self.latest_photo)

    def _drain_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "connected":
                    self.status_var.set(str(payload))
                elif kind == "notice":
                    self.status_var.set(str(payload))
                elif kind == "frame":
                    sequence, width, height, rgb = payload  # type: ignore[misc]
                    output_scale = max(1, OUTPUT_WIDTH // width)
                    output_rgb = scale_nearest(rgb, width, height, output_scale)
                    self.latest_rgb = output_rgb
                    preview_scale = max(1, min(256 // width, 320 // height))
                    preview = scale_nearest(rgb, width, height, preview_scale)
                    self._show_preview(preview, width * preview_scale, height * preview_scale)
                    now = time.monotonic()
                    self._frame_times.append(now)
                    self._frame_times = [stamp for stamp in self._frame_times if now - stamp <= 2.0]
                    fps = max(0.0, (len(self._frame_times) - 1) / max(0.001, now - self._frame_times[0]))
                    self.fps_var.set(f"{fps:.1f} fps - viewer update {sequence}")
                    if self.recording is not None:
                        elapsed = now - self.recording_started
                        expected_frames = max(1, int(elapsed * self.recording.fps) + 1)
                        while self.recording.frames < expected_frames:
                            self.recording.write(output_rgb)
                        self.status_var.set(f"Recording {self.recording.frames} frames ({elapsed:.1f} s) to {self.recording.path.name}")
                elif kind == "error":
                    self.status_var.set(str(payload))
                    messagebox.showerror("Screen stream failed", str(payload), parent=self)
                elif kind == "disconnected":
                    self.connect_button.configure(text="Connect", state="normal")
        except queue.Empty:
            pass
        if not self.closing:
            self.after(50, self._drain_events)

    def snapshot(self) -> None:
        if self.latest_rgb is None:
            messagebox.showinfo("No frame", "Connect and wait for the first live frame.", parent=self)
            return
        filename = filedialog.asksaveasfilename(
            parent=self,
            title="Save RAZ screen snapshot",
            defaultextension=".png",
            filetypes=[("PNG image", "*.png")],
            initialfile=time.strftime("raz-screen-%Y%m%d-%H%M%S.png"),
        )
        if filename:
            write_png(Path(filename), self.latest_rgb, OUTPUT_WIDTH, OUTPUT_HEIGHT)
            self.status_var.set(f"Snapshot saved to {filename}")

    def toggle_recording(self) -> None:
        if self.recording is not None:
            recording = self.recording
            self.recording = None
            recording.close()
            self.record_button.configure(text="Start AVI recording...")
            self.status_var.set(f"Recording saved: {recording.path} ({recording.frames} frames)")
            return
        if self.latest_rgb is None:
            messagebox.showinfo("No frame", "Connect and wait for the first live frame.", parent=self)
            return
        filename = filedialog.asksaveasfilename(
            parent=self,
            title="Record RAZ screen video",
            defaultextension=".avi",
            filetypes=[("AVI video", "*.avi")],
            initialfile=time.strftime("raz-screen-%Y%m%d-%H%M%S.avi"),
        )
        if filename:
            self.recording = AviWriter(Path(filename), OUTPUT_WIDTH, OUTPUT_HEIGHT, RECORDING_FPS)
            self.recording_started = time.monotonic()
            self.record_button.configure(text="Stop recording")
            self.status_var.set(f"Recording to {filename}")

    def close(self) -> None:
        if self.closing:
            return
        self.closing = True
        self.stop_event.set()
        self.withdraw()
        self._cancel_serial_read()
        self.after(25, self._finish_close)

    def _finish_close(self) -> None:
        if self.worker is not None and self.worker.is_alive():
            self.after(25, self._finish_close)
            return
        if self.recording is not None:
            self.recording.close()
            self.recording = None
        self.destroy()


def run_screen_streamer(port: str = "") -> int:
    ScreenStreamer(port).mainloop()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="", help="ESP32 serial port, e.g. COM7")
    args = parser.parse_args()
    return run_screen_streamer(args.port)


if __name__ == "__main__":
    raise SystemExit(main())
