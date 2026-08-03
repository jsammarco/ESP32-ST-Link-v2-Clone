#!/usr/bin/env python3
"""Windows desktop manager for the ESP32 RAZ SWD adapter.

This is a graphical wrapper around fast_flash.py. It keeps the serial protocol
in one well-tested command-line tool while running long operations off the UI
thread and showing the exact ESP32 progress messages in the window.
"""

from __future__ import annotations

import os
import queue
import re
import shutil
import subprocess
import sys
import threading
from datetime import datetime
from pathlib import Path
import tkinter as tk
from tkinter import filedialog, messagebox, scrolledtext, ttk

from launcher_storage import (
    APP_SAFE_BYTES,
    BUNDLE_APPS as LAUNCHER_BUNDLE_APPS,
    FLASH_TOTAL_BYTES,
    PHOTO_ASSET_BYTES,
    SETTINGS_RESERVED_BYTES,
    SLIDESHOW_COMMON_ESTIMATE_BYTES,
    STREAMER_ESTIMATE_BYTES,
    projected_launcher_bytes,
    selected_image_bytes,
)

try:
    from serial.tools import list_ports
except ModuleNotFoundError:
    list_ports = None


def find_repo_root() -> Path:
    candidates = [Path(__file__).resolve().parent.parent]
    if getattr(sys, "frozen", False):
        executable_directory = Path(sys.executable).resolve().parent
        candidates.extend([executable_directory, executable_directory.parent, Path.cwd()])
    for candidate in candidates:
        if (candidate / "platformio.ini").is_file() and (candidate / "RAZ Vape Apps").is_dir():
            return candidate
    return candidates[0]


REPO_ROOT = find_repo_root()
os.environ.setdefault("RAZ_REPO_ROOT", str(REPO_ROOT))
FLASH_TOOL = Path(__file__).resolve().with_name("fast_flash.py")
SLIDESHOW_BUILD_TOOL = Path(__file__).resolve().with_name("build_slideshow_with_photos.py")
LAUNCHER_BUILD_TOOL = Path(__file__).resolve().with_name("build_launcher_with_photos.py")
STREAM_BUILD_TOOL = Path(__file__).resolve().with_name("build_streamable_app.py")
SCREEN_STREAMER_TOOL = Path(__file__).resolve().with_name("raz_screen_streamer.py")
BACKUP_ROOT = REPO_ROOT / "backups"
FULL_FLASH_BYTES = FLASH_TOTAL_BYTES
NV_LINE = re.compile(r"^NV\s+(\d+)\s+(NONE|[0-9A-Fa-f]{8})$")
FLASH_USAGE_LINE = re.compile(r"^FLASH_USAGE\s+(\d+)\s+(\d+)\s+(\d+)")

APPS = {
    "Launcher": REPO_ROOT / "RAZ Vape Apps" / "Launcher" / "build" / "launcher.bin",
    "Tetris": REPO_ROOT / "RAZ Vape Apps" / "Tetris" / "build" / "tetris.bin",
    "Pac-Man": REPO_ROOT / "RAZ Vape Apps" / "Pacman" / "build" / "pacman.bin",
    "Mario 1-1": REPO_ROOT / "RAZ Vape Apps" / "Mario" / "build" / "mario.bin",
    "Geometry Dash": REPO_ROOT / "RAZ Vape Apps" / "GeometryDash" / "build" / "geometry-dash.bin",
    "Chrome Dino": REPO_ROOT / "RAZ Vape Apps" / "ChromeDino" / "build" / "chrome-dino.bin",
    "Tower Stacker": REPO_ROOT / "RAZ Vape Apps" / "TowerStacker" / "build" / "tower-stacker.bin",
    "Slideshow": REPO_ROOT / "RAZ Vape Apps" / "Slideshow" / "build" / "slideshow.bin",
    "Flappy": REPO_ROOT / "RAZ Vape Apps" / "flappy" / "build" / "flappy.bin",
}
SLIDESHOW_CUSTOM_IMAGE = REPO_ROOT / "RAZ Vape Apps" / "Slideshow" / "build" / "slideshow-photos.bin"
LAUNCHER_CUSTOM_IMAGE = REPO_ROOT / "RAZ Vape Apps" / "Launcher" / "build" / "launcher-custom.bin"
LAUNCHER_STREAM_IMAGE = REPO_ROOT / "RAZ Vape Apps" / "Launcher" / "build" / "launcher-custom-stream.bin"
SLIDESHOW_PHOTOS_STREAM_IMAGE = REPO_ROOT / "RAZ Vape Apps" / "Slideshow" / "build" / "slideshow-photos-stream.bin"
STREAM_IMAGES = {
    "Tetris": REPO_ROOT / "RAZ Vape Apps" / "Tetris" / "build" / "tetris-stream.bin",
    "Pac-Man": REPO_ROOT / "RAZ Vape Apps" / "Pacman" / "build" / "pacman-stream.bin",
    "Mario 1-1": REPO_ROOT / "RAZ Vape Apps" / "Mario" / "build" / "mario-stream.bin",
    "Geometry Dash": REPO_ROOT / "RAZ Vape Apps" / "GeometryDash" / "build" / "geometry-dash-stream.bin",
    "Chrome Dino": REPO_ROOT / "RAZ Vape Apps" / "ChromeDino" / "build" / "chrome-dino-stream.bin",
    "Tower Stacker": REPO_ROOT / "RAZ Vape Apps" / "TowerStacker" / "build" / "tower-stacker-stream.bin",
    "Flappy": REPO_ROOT / "RAZ Vape Apps" / "flappy" / "build" / "flappy-stream.bin",
    "Slideshow": REPO_ROOT / "RAZ Vape Apps" / "Slideshow" / "build" / "slideshow-stream.bin",
}
NV_LABELS = {
    0: "Puff count",
    1: "Total vape time",
    2: "Flappy high score",
    3: "Slot spins",
    4: "Slot wins",
    5: "Launcher heater use",
    6: "Launcher factory import",
    7: "Tetris high score",
}
VAPE_EMPTY_TICKS = 340_000
CUSTOM_APP = "Custom .bin..."
INVALID_FOLDER_NAME = re.compile(r'[<>:"/\\|?*\x00-\x1f]')
LAUNCHER_LEVEL_OPTIONS: dict[str, int | None] = {
    "Preserve saved value": None,
    "100% (full display)": 100,
    "75%": 75,
    "50%": 50,
    "25%": 25,
    "0% (empty display)": 0,
}
COIL_PROFILE_OPTIONS = {
    "Current app default": "default",
    "Conservative (reduced output)": "conservative",
    "Coil disabled (UI only)": "disabled",
}


def local_tool_command(tool: Path, arguments: list[str]) -> list[str]:
    if getattr(sys, "frozen", False):
        return [sys.executable, "--internal-tool", tool.stem, *arguments]
    return [sys.executable, "-u", str(tool), *arguments]


def local_tool_available(tool: Path) -> bool:
    return getattr(sys, "frozen", False) or tool.is_file()


class RazManager(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("RAZ ESP32 Manager")
        self.minsize(780, 1020)
        self.geometry("880x1220")
        self.option_add("*tearOff", False)

        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.process: subprocess.Popen[str] | None = None
        self.stream_process: subprocess.Popen[object] | None = None
        self.current_action = ""
        self.cancel_requested = False
        self.values: dict[int, int | None] = {}
        # A queued action is (label, command arguments, uses_fast_flash_tool).
        # Build steps run as ordinary local commands; hardware steps use the
        # selected ESP32 serial port through fast_flash.py.
        self.pending_steps: list[tuple[str, list[str], bool]] = []
        self.port_var = tk.StringVar()
        self.app_var = tk.StringVar(value="Launcher")
        self.backup_name_var = tk.StringVar()
        self.backup_before_flash_var = tk.BooleanVar(value=True)
        self.screen_stream_var = tk.BooleanVar(value=False)
        self.launcher_level_var = tk.StringVar(value="Preserve saved value")
        self.coil_profile_var = tk.StringVar(value="Current app default")
        self.launcher_app_vars = {
            app: tk.BooleanVar(value=app in {"Tetris", "Flappy"})
            for app in LAUNCHER_BUNDLE_APPS
        }
        self.launcher_app_checks: dict[str, ttk.Checkbutton] = {}
        self.custom_image_path: Path | None = None
        self.custom_image_var = tk.StringVar(value="Launcher will be built with Tetris + Flappy.")
        self.storage_detail_var = tk.StringVar()
        self.storage_over_limit = False
        self.built_storage_bytes: int | None = None
        self.slideshow_photos: list[Path] = []
        self.slideshow_photo_var = tk.StringVar(
            value="No custom embedded Slideshow photos selected — the bundled app image will be used."
        )
        self.status_var = tk.StringVar(value="Choose the ESP32 COM port, then use a read-only action first.")
        self.value_text_var = tk.StringVar(value="No values read yet.")
        self.action_widgets: list[tk.Widget] = []

        self._build_ui()
        self.update_storage_bar()
        self.refresh_ports()
        self.after(75, self._drain_events)

    def _build_ui(self) -> None:
        main = ttk.Frame(self, padding=14)
        main.grid(sticky="nsew")
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)
        main.columnconfigure(1, weight=1)
        main.columnconfigure(3, weight=0)
        main.rowconfigure(7, weight=1)

        ttk.Label(main, text="RAZ ESP32 Manager", font=("Segoe UI", 16, "bold")).grid(
            row=0, column=0, columnspan=4, sticky="w"
        )
        ttk.Label(
            main,
            text="Use the direct ESP32 serial adapter. Close the old serial bridge and any serial monitor first.",
            foreground="#444444",
        ).grid(row=1, column=0, columnspan=4, sticky="w", pady=(2, 12))

        ttk.Label(main, text="ESP32 port:").grid(row=2, column=0, sticky="w")
        self.port_box = ttk.Combobox(main, textvariable=self.port_var, width=22, state="normal")
        self.port_box.grid(row=2, column=1, sticky="w")
        refresh = ttk.Button(main, text="Refresh ports", command=self.refresh_ports)
        refresh.grid(row=2, column=2, sticky="w", padx=(8, 0))
        update_esp32 = ttk.Button(main, text="Update ESP32 firmware...", command=self.update_esp32_firmware)
        update_esp32.grid(row=2, column=3, sticky="w", padx=(8, 0))
        self.action_widgets.extend([refresh, update_esp32])

        mode_frame = ttk.LabelFrame(main, text="ESP32 operating mode", padding=10)
        mode_frame.grid(row=3, column=0, columnspan=4, sticky="ew", pady=(14, 0))
        get_mode = ttk.Button(mode_frame, text="Get current mode", command=self.get_esp32_mode)
        get_mode.grid(row=0, column=0, sticky="w")
        programmer_mode = ttk.Button(
            mode_frame,
            text="Enable SWD programmer",
            command=self.enable_esp32_programmer,
        )
        programmer_mode.grid(row=0, column=1, sticky="w", padx=(8, 0))
        runtime_mode = ttk.Button(
            mode_frame,
            text="Enable Wi-Fi runtime",
            command=self.enable_esp32_runtime,
        )
        runtime_mode.grid(row=0, column=2, sticky="w", padx=(8, 0))
        self.action_widgets.extend([get_mode, programmer_mode, runtime_mode])
        ttk.Label(
            mode_frame,
            text=("One integrated ESP32 image provides both modes. Shared GPIO25/GPIO26 stay high-Z while "
                  "the programmer is idle; Wi-Fi runtime only drives its TX line after the vape sends PING."),
            foreground="#444444",
            wraplength=760,
        ).grid(row=1, column=0, columnspan=3, sticky="w", pady=(8, 0))

        read_frame = ttk.LabelFrame(main, text="Read only", padding=10)
        read_frame.grid(row=4, column=0, columnspan=4, sticky="ew", pady=(12, 0))
        probe = ttk.Button(read_frame, text="Test connection", command=self.probe)
        probe.grid(row=0, column=0, sticky="w")
        values = ttk.Button(read_frame, text="Get saved vape values", command=self.read_values)
        values.grid(row=0, column=1, sticky="w", padx=(8, 0))
        self.action_widgets.extend([probe, values])
        ttk.Label(
            read_frame,
            text=("Values read the internal-flash NV keys used by the bundled apps. "
                  "Launcher stores heater-use ticks, not a discrete puff count."),
            wraplength=650,
        ).grid(row=1, column=0, columnspan=2, sticky="w", pady=(8, 0))

        write_frame = ttk.LabelFrame(main, text="Backup, restore, and app flash", padding=10)
        write_frame.grid(row=5, column=0, columnspan=4, sticky="ew", pady=(12, 0))
        write_frame.columnconfigure(1, weight=1)
        flash_tip = tk.Label(
            write_frame,
            text=("PROGRAMMING TIP: After confirming Flash, hold the vape's physical button. "
                  "Keep holding until the operation log shows ERASE 1/...; then release. "
                  "If automatic backup is enabled, it runs before erasing starts."),
            anchor="w",
            justify="left",
            wraplength=700,
            bg="#fff3cd",
            fg="#6b4b00",
            font=("Segoe UI", 11, "bold"),
            padx=10,
            pady=8,
        )
        flash_tip.grid(row=0, column=0, columnspan=4, sticky="ew", pady=(0, 10))
        ttk.Label(write_frame, text="Backup folder name:").grid(row=1, column=0, sticky="w")
        backup_name = ttk.Entry(write_frame, textvariable=self.backup_name_var, width=28)
        backup_name.grid(row=1, column=1, sticky="ew", padx=(8, 0))
        backup = ttk.Button(write_frame, text="Back up current device", command=self.backup)
        backup.grid(row=1, column=2, sticky="w", padx=(8, 0))
        restore = ttk.Button(write_frame, text="Restore backup / .bin...", command=self.restore)
        restore.grid(row=1, column=3, sticky="w", padx=(8, 0))
        self.action_widgets.extend([backup_name, backup, restore])
        ttk.Label(
            write_frame,
            text="Optional. Leave blank for an automatic raz-YYYYMMDD-HHMMSS folder under backups.",
            foreground="#444444",
        ).grid(row=2, column=1, columnspan=3, sticky="w", padx=(8, 0), pady=(2, 0))

        ttk.Label(write_frame, text="App:").grid(row=3, column=0, sticky="w", pady=(12, 0))
        app_box = ttk.Combobox(write_frame, textvariable=self.app_var, values=[*APPS, CUSTOM_APP], state="readonly", width=20)
        app_box.grid(row=3, column=1, sticky="w", padx=(8, 0), pady=(12, 0))
        app_box.bind("<<ComboboxSelected>>", self.on_app_selected)
        flash = ttk.Button(write_frame, text="Flash selected app...", command=self.flash_app)
        flash.grid(row=3, column=2, sticky="w", padx=(8, 0), pady=(12, 0))
        self.action_widgets.extend([app_box, flash])
        ttk.Label(write_frame, textvariable=self.custom_image_var, foreground="#444444", wraplength=610).grid(
            row=4, column=1, columnspan=3, sticky="w", padx=(8, 0), pady=(3, 0)
        )

        ttk.Label(write_frame, text="Launcher bundle:").grid(row=5, column=0, sticky="nw", pady=(12, 0))
        launcher_bundle_frame = ttk.Frame(write_frame)
        launcher_bundle_frame.grid(row=5, column=1, columnspan=3, sticky="w", padx=(8, 0), pady=(8, 0))
        for index, app in enumerate(LAUNCHER_BUNDLE_APPS):
            check = ttk.Checkbutton(
                launcher_bundle_frame,
                text=app,
                variable=self.launcher_app_vars[app],
                command=self.on_launcher_bundle_changed,
            )
            check.grid(row=index // 3, column=index % 3, sticky="w", padx=(0, 18), pady=2)
            self.launcher_app_checks[app] = check
            self.action_widgets.append(check)

        ttk.Label(write_frame, text="Flash storage:").grid(row=6, column=0, sticky="nw", pady=(12, 0))
        storage_frame = ttk.Frame(write_frame)
        storage_frame.grid(row=6, column=1, columnspan=3, sticky="ew", padx=(8, 0), pady=(10, 0))
        storage_frame.columnconfigure(0, weight=1)
        self.storage_canvas = tk.Canvas(
            storage_frame,
            height=24,
            width=560,
            highlightthickness=1,
            highlightbackground="#8993a4",
            bg="#e8edf3",
        )
        self.storage_canvas.grid(row=0, column=0, sticky="ew")
        self.storage_canvas.bind("<Configure>", lambda _event: self.update_storage_bar())
        ttk.Label(
            storage_frame,
            textvariable=self.storage_detail_var,
            foreground="#444444",
            wraplength=610,
        ).grid(row=1, column=0, sticky="w", pady=(3, 0))

        ttk.Label(write_frame, text="Embedded photos:").grid(row=7, column=0, sticky="w", pady=(10, 0))
        self.choose_slideshow_photos_button = ttk.Button(
            write_frame,
            text="Choose up to 3 photos...",
            command=self.choose_slideshow_photos,
        )
        self.choose_slideshow_photos_button.grid(row=7, column=1, sticky="w", padx=(8, 0), pady=(10, 0))
        self.clear_slideshow_photos_button = ttk.Button(
            write_frame,
            text="Clear photos",
            command=self.clear_slideshow_photos,
        )
        self.clear_slideshow_photos_button.grid(row=7, column=2, sticky="w", padx=(8, 0), pady=(10, 0))
        self.action_widgets.extend([self.choose_slideshow_photos_button, self.clear_slideshow_photos_button])
        ttk.Label(write_frame, textvariable=self.slideshow_photo_var, foreground="#444444", wraplength=610).grid(
            row=8, column=1, columnspan=3, sticky="w", padx=(8, 0), pady=(3, 0)
        )
        backup_before_flash = ttk.Checkbutton(
            write_frame,
            text="Create a backup before flashing",
            variable=self.backup_before_flash_var,
        )
        backup_before_flash.grid(row=9, column=1, columnspan=2, sticky="w", padx=(8, 0), pady=(10, 0))
        self.action_widgets.append(backup_before_flash)

        self.screen_stream_check = ttk.Checkbutton(
            write_frame,
            text="Add full-resolution SWD screen streamer (128×160)",
            variable=self.screen_stream_var,
            command=self.on_storage_option_changed,
        )
        self.screen_stream_check.grid(row=10, column=1, columnspan=2, sticky="w", padx=(8, 0), pady=(6, 0))
        self.open_stream_button = ttk.Button(
            write_frame,
            text="Open screen viewer",
            command=self.open_screen_viewer,
        )
        self.open_stream_button.grid(row=10, column=3, sticky="w", padx=(8, 0), pady=(6, 0))
        self.action_widgets.extend([self.screen_stream_check, self.open_stream_button])

        ttk.Label(write_frame, text="Launcher level:").grid(row=11, column=0, sticky="w", pady=(10, 0))
        self.launcher_level_box = ttk.Combobox(
            write_frame,
            textvariable=self.launcher_level_var,
            values=list(LAUNCHER_LEVEL_OPTIONS),
            state="readonly",
            width=25,
        )
        self.launcher_level_box.grid(row=11, column=1, sticky="w", padx=(8, 0), pady=(10, 0))

        ttk.Label(write_frame, text="Coil profile:").grid(row=12, column=0, sticky="w", pady=(6, 0))
        self.coil_profile_box = ttk.Combobox(
            write_frame,
            textvariable=self.coil_profile_var,
            values=list(COIL_PROFILE_OPTIONS),
            state="readonly",
            width=29,
        )
        self.coil_profile_box.grid(row=12, column=1, sticky="w", padx=(8, 0), pady=(6, 0))
        self.action_widgets.extend([self.launcher_level_box, self.coil_profile_box])
        ttk.Label(
            write_frame,
            text=("Launcher-only options. ‘100%’ resets its on-screen remaining-use tracker; it does not recharge "
                  "the battery or consumable. No profile increases the current app's output or cutoff."),
            foreground="#444444",
            wraplength=650,
        ).grid(row=13, column=1, columnspan=3, sticky="w", padx=(8, 0), pady=(4, 0))
        ttk.Label(
            write_frame,
            text="Always create a backup before flashing or restoring. Restore overwrites all internal flash and saved settings.",
            foreground="#7a3000",
            wraplength=650,
        ).grid(row=14, column=0, columnspan=4, sticky="w", pady=(10, 0))

        values_frame = ttk.LabelFrame(main, text="Saved values", padding=10)
        values_frame.grid(row=6, column=0, columnspan=4, sticky="new", pady=(12, 0))
        ttk.Label(values_frame, textvariable=self.value_text_var, justify="left", wraplength=760).grid(sticky="w")

        log_frame = ttk.LabelFrame(main, text="Operation log", padding=8)
        log_frame.grid(row=7, column=0, columnspan=4, sticky="nsew", pady=(12, 0))
        main.rowconfigure(7, weight=1)
        self.log = scrolledtext.ScrolledText(log_frame, height=8, wrap="word", state="disabled", font=("Cascadia Mono", 9))
        self.log.grid(sticky="nsew")
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)

        status_frame = ttk.Frame(main)
        status_frame.grid(row=8, column=0, columnspan=4, sticky="ew", pady=(12, 0))
        status_frame.columnconfigure(0, weight=1)
        status = ttk.Label(status_frame, textvariable=self.status_var, relief="sunken", anchor="w")
        status.grid(row=0, column=0, sticky="ew")
        self.cancel_button = ttk.Button(
            status_frame,
            text="Cancel / stop waiting",
            command=self.cancel_current_operation,
            state="disabled",
        )
        self.cancel_button.grid(row=0, column=1, sticky="e", padx=(8, 0))

        self.update_launcher_options()
        self.update_slideshow_photo_options()

    def refresh_ports(self) -> None:
        ports: list[str] = []
        if list_ports is not None:
            # COM1 is normally the legacy motherboard serial port. Never
            # auto-select or offer it as the ESP32 adapter port.
            ports = [port.device for port in list_ports.comports() if port.device.upper() != "COM1"]
        self.port_box["values"] = ports
        if ports and not self.port_var.get().strip():
            self.port_var.set(ports[0])
        self.status_var.set("Ports refreshed. COM1 is excluded; select the ESP32 DevKit COM port.")

    def on_app_selected(self, _event: tk.Event[tk.Misc] | None = None) -> None:
        self.built_storage_bytes = None
        selection = self.app_var.get()
        if selection == CUSTOM_APP:
            self.screen_stream_var.set(False)
            self.choose_custom_image()
        elif selection == "Launcher":
            self.update_launcher_bundle_summary()
        else:
            self.custom_image_var.set(f"Bundled image: {APPS[selection]}")
        self.update_launcher_options()
        self.update_slideshow_photo_options()
        self.update_storage_bar()

    def selected_launcher_apps(self) -> list[str]:
        return [app for app in LAUNCHER_BUNDLE_APPS if self.launcher_app_vars[app].get()]

    def bundled_slideshow_photo_count(self) -> int:
        if self.slideshow_photos:
            return len(self.slideshow_photos)
        header = REPO_ROOT / "RAZ Vape Apps" / "Launcher" / "generated" / "photos.h"
        try:
            match = re.search(r"#define\s+SLIDESHOW_IMAGE_COUNT\s+(\d+)u?", header.read_text(encoding="ascii"))
        except OSError:
            return 0
        return int(match.group(1)) if match else 0

    def current_storage_usage(self) -> tuple[int, bool, str]:
        selection = self.app_var.get()
        screen_stream = self.screen_stream_var.get() and selection != CUSTOM_APP
        if selection == CUSTOM_APP:
            if self.custom_image_path is None or not self.custom_image_path.is_file():
                return 0, False, "Choose a custom image to measure it"
            return self.custom_image_path.stat().st_size, True, "Exact custom image"
        if selection == "Launcher":
            apps = self.selected_launcher_apps()
            if not apps:
                return 0, False, "Select at least one Launcher app"
            if self.built_storage_bytes is not None:
                return self.built_storage_bytes, True, "Exact linked Launcher image"
            photo_count = self.bundled_slideshow_photo_count() if "Slideshow" in apps else 0
            return (
                projected_launcher_bytes(
                    apps,
                    screen_stream=screen_stream,
                    photo_count=photo_count,
                ),
                False,
                "Projected Launcher build",
            )
        if selection == "Slideshow" and self.slideshow_photos:
            size = SLIDESHOW_COMMON_ESTIMATE_BYTES + len(self.slideshow_photos) * PHOTO_ASSET_BYTES
            if screen_stream:
                size += STREAMER_ESTIMATE_BYTES
            return size, False, "Projected custom Slideshow build"
        normal = APPS.get(selection)
        if normal is None:
            return 0, False, "No image selected"
        size, exact = selected_image_bytes(normal, STREAM_IMAGES.get(selection), screen_stream)
        return size, exact, "Exact built image" if exact else "Projected image"

    def update_storage_bar(self) -> None:
        if not hasattr(self, "storage_canvas"):
            return
        used, exact, source = self.current_storage_usage()
        self.storage_over_limit = used > APP_SAFE_BYTES
        width = max(self.storage_canvas.winfo_width(), 520)
        height = 24
        safe_x = int(width * APP_SAFE_BYTES / FLASH_TOTAL_BYTES)
        used_x = min(width, int(width * used / FLASH_TOTAL_BYTES)) if used else 0
        if self.storage_over_limit:
            fill = "#d64545"
        elif used >= int(APP_SAFE_BYTES * 0.8):
            fill = "#e5a72b"
        else:
            fill = "#2c9c69"

        self.storage_canvas.delete("all")
        self.storage_canvas.create_rectangle(0, 0, width, height, fill="#e8edf3", outline="")
        self.storage_canvas.create_rectangle(safe_x, 0, width, height, fill="#d9c89b", outline="")
        if used_x:
            self.storage_canvas.create_rectangle(0, 0, used_x, height, fill=fill, outline="")
        self.storage_canvas.create_line(safe_x, 0, safe_x, height, fill="#765d20", width=2)
        status = "OVER SAFE LIMIT" if self.storage_over_limit else f"{used / 1024:.1f} KB used"
        self.storage_canvas.create_text(
            width // 2,
            height // 2,
            text=status,
            fill="#ffffff" if used_x > width // 2 or self.storage_over_limit else "#243247",
            font=("Segoe UI", 9, "bold"),
        )
        accuracy = "exact" if exact else "estimate"
        remaining = APP_SAFE_BYTES - used
        if remaining >= 0:
            detail = (
                f"{source} ({accuracy}): {used:,} of {APP_SAFE_BYTES:,} app bytes; "
                f"{remaining:,} free. {SETTINGS_RESERVED_BYTES:,} bytes reserved for saved values "
                f"({FLASH_TOTAL_BYTES:,} total)."
            )
        else:
            detail = (
                f"{source} ({accuracy}): {used:,} bytes, {abs(remaining):,} over the safe app limit. "
                "Deselect apps/photos or disable streaming."
            )
        self.storage_detail_var.set(detail)

    def update_launcher_bundle_summary(self) -> None:
        apps = self.selected_launcher_apps()
        if not apps:
            self.custom_image_var.set("Select at least one app for the Launcher bundle.")
        else:
            self.custom_image_var.set(
                f"Launcher will be built with {len(apps)} app(s): " + ", ".join(apps) + "."
            )

    def on_launcher_bundle_changed(self, _event: tk.Event[tk.Misc] | None = None) -> None:
        self.built_storage_bytes = None
        self.update_launcher_bundle_summary()
        self.update_slideshow_photo_options()
        self.update_storage_bar()

    def on_storage_option_changed(self) -> None:
        self.built_storage_bytes = None
        self.update_storage_bar()

    def update_launcher_options(self) -> None:
        launcher_state = "normal" if self.app_var.get() == "Launcher" and self.process is None else "disabled"
        for check in self.launcher_app_checks.values():
            check.configure(state=launcher_state)
        combo_state = "readonly" if launcher_state == "normal" else "disabled"
        self.launcher_level_box.configure(state=combo_state)
        self.coil_profile_box.configure(state=combo_state)
        stream_state = "normal" if self.app_var.get() != CUSTOM_APP and self.process is None else "disabled"
        self.screen_stream_check.configure(state=stream_state)

    def update_slideshow_photo_options(self) -> None:
        slideshow_available = self.app_var.get() == "Slideshow" or (
            self.app_var.get() == "Launcher" and "Slideshow" in self.selected_launcher_apps()
        )
        state = "normal" if slideshow_available and self.process is None else "disabled"
        self.choose_slideshow_photos_button.configure(state=state)
        self.clear_slideshow_photos_button.configure(state=state)

    def open_screen_viewer(self) -> None:
        port = self.selected_port()
        if port is None or self.process is not None:
            return
        if self.stream_process is not None and self.stream_process.poll() is None:
            return

        if getattr(sys, "frozen", False):
            command = [sys.executable, "--screen-streamer", "--port", port]
        else:
            if not SCREEN_STREAMER_TOOL.is_file():
                messagebox.showerror("Missing screen viewer", f"Cannot find:\n{SCREEN_STREAMER_TOOL}")
                return
            command = [sys.executable, str(SCREEN_STREAMER_TOOL), "--port", port]
        try:
            self.stream_process = subprocess.Popen(command, cwd=REPO_ROOT)
        except OSError as exc:
            messagebox.showerror("Could not open screen viewer", str(exc))
            return
        self.set_actions_enabled(False)
        self.status_var.set("Screen viewer owns the ESP32 port. Close it before using manager operations.")
        self.after(250, self._poll_screen_viewer)

    def _poll_screen_viewer(self) -> None:
        process = self.stream_process
        if process is not None and process.poll() is None:
            self.after(250, self._poll_screen_viewer)
            return
        self.stream_process = None
        if self.process is None:
            self.set_actions_enabled(True)
            self.status_var.set("Screen viewer closed; manager operations are available again.")

    def choose_slideshow_photos(self) -> None:
        selected = filedialog.askopenfilenames(
            parent=self,
            title="Choose one to three embedded Slideshow photos",
            initialdir=Path.home(),
            filetypes=[
                ("Supported images", "*.bmp *.gif *.jpeg *.jpg *.png *.webp"),
                ("All files", "*.*"),
            ],
        )
        if not selected:
            return
        if len(selected) > 3:
            messagebox.showerror("Too many photos", "Choose no more than three photos for the embedded Slideshow.")
            return
        photos = [Path(path) for path in selected]
        if len({path.resolve() for path in photos}) != len(photos):
            messagebox.showerror("Duplicate photo", "Choose each Slideshow photo only once.")
            return
        self.slideshow_photos = photos
        self.built_storage_bytes = None
        names = ", ".join(path.name for path in photos)
        self.slideshow_photo_var.set(f"{len(photos)} photo(s) selected: {names}")
        target = "Launcher Slideshow slot" if self.app_var.get() == "Launcher" else "Slideshow"
        self.status_var.set(f"Selected photos will be embedded when {target} is flashed.")
        self.update_storage_bar()

    def clear_slideshow_photos(self) -> None:
        self.slideshow_photos.clear()
        self.built_storage_bytes = None
        self.slideshow_photo_var.set(
            "No custom embedded Slideshow photos selected — the bundled app image will be used."
        )
        self.status_var.set("Custom embedded Slideshow photo selection cleared.")
        self.update_storage_bar()

    def choose_custom_image(self) -> Path | None:
        image = filedialog.askopenfilename(
            parent=self,
            title="Choose a firmware image",
            initialdir=REPO_ROOT,
            filetypes=[("Firmware image", "*.bin"), ("All files", "*.*")],
        )
        if not image:
            self.status_var.set("No custom firmware image selected.")
            return None
        self.custom_image_path = Path(image)
        self.built_storage_bytes = None
        self.custom_image_var.set(f"Custom image: {self.custom_image_path}")
        self.status_var.set(f"Custom firmware selected: {self.custom_image_path.name}")
        self.update_storage_bar()
        return self.custom_image_path

    def selected_port(self) -> str | None:
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("ESP32 port required", "Choose or type the ESP32 COM port first (for example, COM7).")
            return None
        if port.upper() == "COM1":
            messagebox.showerror(
                "COM1 is not an ESP32 port",
                "COM1 is excluded because it is normally the legacy motherboard serial port. Choose the ESP32's assigned COM port.",
            )
            return None
        return port

    def append_log(self, text: str) -> None:
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def set_actions_enabled(self, enabled: bool) -> None:
        for widget in self.action_widgets:
            if not enabled:
                widget.configure(state="disabled")
            elif isinstance(widget, ttk.Combobox):
                widget.configure(state="readonly")
            else:
                widget.configure(state="normal")
        if enabled:
            self.update_launcher_options()
            self.update_slideshow_photo_options()
            self.update_storage_bar()

    def run_tool(self, action: str, arguments: list[str]) -> None:
        port = self.selected_port()
        if port is None or self.process is not None:
            return
        if not local_tool_available(FLASH_TOOL):
            messagebox.showerror("Missing tool", f"Cannot find:\n{FLASH_TOOL}")
            return

        command = local_tool_command(FLASH_TOOL, ["--port", port, *arguments])
        self.run_command(action, command)

    def run_command(self, action: str, command: list[str]) -> None:
        if self.process is not None:
            return
        self.append_log("\n> " + subprocess.list2cmdline(command))
        self.status_var.set(f"{action} is running. Do not unplug the ESP32 or target.")
        self.current_action = action
        self.cancel_requested = False
        self.values = {} if action == "Reading saved values" else self.values
        self.set_actions_enabled(False)
        try:
            self.process = subprocess.Popen(
                command,
                cwd=REPO_ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
        except OSError as exc:
            self.process = None
            self.set_actions_enabled(True)
            messagebox.showerror("Could not start operation", str(exc))
            return
        self.cancel_button.configure(state="normal")
        threading.Thread(target=self._read_process, args=(self.process,), daemon=True).start()

    def cancel_current_operation(self) -> None:
        process = self.process
        if process is None:
            return

        action = self.current_action or "Current operation"
        if not messagebox.askyesno(
            "Cancel PC-side operation?",
            f"Stop waiting for {action}?\n\n"
            "If the ESP32 has already started erasing or programming, it continues locally. "
            "Do not unplug the ESP32 or vape; wait for the vape to restart before trying another operation.",
            icon="warning",
        ):
            return

        self.cancel_requested = True
        self.pending_steps.clear()
        self.cancel_button.configure(state="disabled")
        self.status_var.set(
            f"Stopping the PC-side {action} process. Keep the ESP32 and vape connected if programming had started."
        )
        try:
            process.terminate()
        except OSError as exc:
            self.append_log(f"Could not terminate the PC-side process: {exc}")

    def platformio_executable(self) -> str | None:
        for command in ("pio", "platformio"):
            located = shutil.which(command)
            if located:
                return located
        bundled = Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe"
        if bundled.is_file():
            return str(bundled)
        return None

    def update_esp32_firmware(self) -> None:
        port = self.selected_port()
        if port is None or self.process is not None:
            return
        platformio = self.platformio_executable()
        if platformio is None:
            messagebox.showerror(
                "PlatformIO not found",
                "Could not find pio or platformio.exe. Install PlatformIO, or ensure its command is on PATH.",
            )
            return
        if not messagebox.askyesno(
            "Update ESP32 firmware?",
            "This uploads the integrated ESP32 SWD-programmer and Wi-Fi/browser-runtime firmware "
            "to the selected COM port.\n\n"
            "It does not flash or erase the vape. The ESP32 will restart after the upload.\n\n"
            f"Update the ESP32 on {port}?",
            icon="warning",
        ):
            return
        self.run_command(
            "Update ESP32 firmware",
            [platformio, "run", "--target", "upload", "--upload-port", port],
        )

    def get_esp32_mode(self) -> None:
        self.run_tool("Reading ESP32 mode", ["--esp32-mode"])

    def enable_esp32_runtime(self) -> None:
        port = self.selected_port()
        if port is None or self.process is not None:
            return
        if not messagebox.askyesno(
            "Enable Wi-Fi runtime?",
            "This persistently switches the ESP32 from SWD programming to the 9,600-baud "
            "RAZ Wi-Fi runtime. SWD operations remain unavailable until programmer mode is restored.\n\n"
            "The vape must already contain the N32 menu firmware. This action does not flash the vape "
            "and does not connect the ESP32 to any network.\n\n"
            f"Enable Wi-Fi runtime on {port}?",
            icon="question",
        ):
            return
        self.run_tool("Enable Wi-Fi runtime", ["--esp32-runtime"])

    def enable_esp32_programmer(self) -> None:
        port = self.selected_port()
        if port is None or self.process is not None:
            return
        if not messagebox.askyesno(
            "Enable SWD programmer?",
            "This stops Wi-Fi/UART runtime and persistently restores SWD programmer mode. "
            "The shared communication pins remain high-Z until a programming operation starts.\n\n"
            "For N32 recovery, hold the vape button while resetting or powering the vape, then use "
            "Test connection.\n\n"
            f"Enable SWD programmer on {port}?",
            icon="question",
        ):
            return
        self.run_tool("Enable SWD programmer", ["--esp32-programmer"])

    def _read_process(self, process: subprocess.Popen[str]) -> None:
        assert process.stdout is not None
        for raw_line in process.stdout:
            self.events.put(("line", raw_line.rstrip("\r\n")))
        self.events.put(("complete", process.wait()))

    def _drain_events(self) -> None:
        try:
            while True:
                kind, payload = self.events.get_nowait()
                if kind == "line":
                    line = str(payload)
                    self.append_log(line)
                    match = NV_LINE.match(line)
                    if match:
                        key = int(match.group(1))
                        raw_value = match.group(2)
                        self.values[key] = None if raw_value == "NONE" else int(raw_value, 16)
                    flash_match = FLASH_USAGE_LINE.match(line)
                    if flash_match:
                        self.built_storage_bytes = int(flash_match.group(1))
                        self.update_storage_bar()
                elif kind == "complete":
                    self._complete_action(int(payload))
        except queue.Empty:
            pass
        self.after(75, self._drain_events)

    def start_next_pending_step(self) -> None:
        """Start the next queued local-build or ESP32 operation."""
        next_action, next_arguments, uses_fast_flash_tool = self.pending_steps.pop(0)
        self.append_log(f"Starting next queued step: {next_action}")
        if uses_fast_flash_tool:
            self.run_tool(next_action, next_arguments)
        else:
            self.run_command(next_action, next_arguments)

    def _complete_action(self, return_code: int) -> None:
        action = self.current_action
        self.process = None
        self.current_action = ""
        self.cancel_button.configure(state="disabled")
        if self.cancel_requested:
            self.cancel_requested = False
            self.pending_steps.clear()
            self.set_actions_enabled(True)
            self.append_log(f"{action} cancelled on the PC.")
            self.status_var.set(
                "PC-side wait cancelled. If programming had started, keep hardware connected until the vape restarts."
            )
            return
        if return_code == 0:
            if self.pending_steps:
                self.start_next_pending_step()
                return
            self.set_actions_enabled(True)
            if action == "Reading saved values":
                self.value_text_var.set(self.format_values())
            self.status_var.set(f"{action} completed successfully.")
        else:
            self.pending_steps.clear()
            self.set_actions_enabled(True)
            self.status_var.set(f"{action} failed. See the operation log.")
            messagebox.showerror("Operation failed", f"{action} exited with code {return_code}.\n\nSee the operation log for details.")

    def format_values(self) -> str:
        if not self.values:
            return "No saved values were returned. Ensure the latest ESP32 firmware is installed."
        lines: list[str] = []
        for key in range(8):
            label = NV_LABELS[key]
            value = self.values.get(key)
            if value is None:
                lines.append(f"{label}: not stored")
            elif key == 5:
                capped = min(value, VAPE_EMPTY_TICKS)
                remaining = max(0, ((VAPE_EMPTY_TICKS - capped) * 100) // VAPE_EMPTY_TICKS)
                bars = 0 if capped >= VAPE_EMPTY_TICKS else 6 - (capped // 60_000)
                lines.append(
                    f"{label}: {value:,} ticks ({value / 100:.2f} seconds used; {remaining}% / {bars} bars remaining)"
                )
            elif key == 1:
                lines.append(f"{label}: {value:,} ticks ({value / 100:.2f} seconds)")
            else:
                lines.append(f"{label}: {value:,}")
        return "\n".join(lines)

    def probe(self) -> None:
        self.run_tool("Connection test", ["--probe"])

    def read_values(self) -> None:
        self.run_tool("Reading saved values", ["--values"])

    def make_backup_destination(self) -> Path | None:
        BACKUP_ROOT.mkdir(exist_ok=True)
        requested_name = self.backup_name_var.get().strip()
        if requested_name:
            if (
                requested_name in {".", ".."}
                or Path(requested_name).name != requested_name
                or INVALID_FOLDER_NAME.search(requested_name)
            ):
                messagebox.showerror(
                    "Invalid backup name",
                    "Use a simple folder name without slashes or these characters: < > : \" / \\ | ? *",
                )
                return None
            destination = BACKUP_ROOT / requested_name
            if destination.exists():
                messagebox.showerror(
                    "Backup already exists",
                    f"Choose a different backup folder name.\n\nExisting folder:\n{destination}",
                )
                return None
        else:
            stamp = datetime.now().strftime("raz-%Y%m%d-%H%M%S")
            destination = BACKUP_ROOT / stamp
            suffix = 2
            while destination.exists():
                destination = BACKUP_ROOT / f"{stamp}-{suffix}"
                suffix += 1
        return destination

    def backup(self) -> None:
        destination = self.make_backup_destination()
        if destination is None:
            return
        self.run_tool("Backup", ["--backup", str(destination)])

    def restore(self) -> None:
        initial = BACKUP_ROOT if BACKUP_ROOT.is_dir() else REPO_ROOT
        source_kind = messagebox.askyesnocancel(
            "Restore source",
            "Restore from a standalone 64 KB internal-flash .bin file?\n\n"
            "Yes: choose a .bin file\n"
            "No: choose a standard backup folder\n"
            "Cancel: do nothing",
            icon="question",
        )
        if source_kind is None:
            return

        if source_kind:
            selected = filedialog.askopenfilename(
                title="Choose a 64 KB internal-flash backup image",
                initialdir=initial,
                filetypes=[("Internal-flash backup", "*.bin"), ("All files", "*.*")],
            )
        else:
            selected = filedialog.askdirectory(title="Choose a RAZ backup folder", initialdir=initial)
        if not selected:
            return

        backup = Path(selected)
        image_path = backup if source_kind else backup / "internal_flash.bin"
        if not image_path.is_file():
            expected = "a .bin file" if source_kind else "internal_flash.bin"
            messagebox.showerror("Invalid backup", f"Choose {expected} containing the full internal-flash image.")
            return
        if image_path.stat().st_size != FULL_FLASH_BYTES:
            messagebox.showerror(
                "Invalid backup size",
                f"Restore requires an exact {FULL_FLASH_BYTES:,}-byte full internal-flash image.\n\n"
                f"Selected: {image_path.name} ({image_path.stat().st_size:,} bytes)",
            )
            return

        source_note = (
            "The folder manifest, when present, will be checked before restore."
            if not source_kind
            else "This standalone image has no manifest; the ESP32 will still program and verify all 64 KB."
        )
        if not messagebox.askyesno(
            "Restore full backup?",
            "This erases and replaces all 64 KB of internal flash, including the installed app and saved settings.\n\n"
            f"Selected: {image_path.name}\n{source_note}\n\n"
            "Restore the selected backup now?",
            icon="warning",
        ):
            return
        self.run_tool("Restore", ["--restore", str(backup), "--confirm-restore"])

    def flash_app(self) -> None:
        if self.selected_port() is None or self.process is not None:
            return
        selection = self.app_var.get()
        launcher_apps: list[str] = []
        if selection == "Launcher":
            launcher_apps = self.selected_launcher_apps()
            if not launcher_apps:
                messagebox.showerror("Launcher app required", "Select at least one app for the Launcher bundle.")
                return
        slideshow_selected = selection == "Slideshow" or (
            selection == "Launcher" and "Slideshow" in launcher_apps
        )
        building_selected_photos = slideshow_selected and bool(self.slideshow_photos)
        stream_enabled = self.screen_stream_var.get() and selection != CUSTOM_APP
        if selection == CUSTOM_APP:
            if self.custom_image_path is None or not self.custom_image_path.is_file():
                if self.choose_custom_image() is None:
                    return
            image_path = self.custom_image_path
            if image_path is None:
                return
        elif selection == "Launcher":
            image_path = LAUNCHER_STREAM_IMAGE if stream_enabled else LAUNCHER_CUSTOM_IMAGE
        elif building_selected_photos:
            image_path = SLIDESHOW_PHOTOS_STREAM_IMAGE if stream_enabled else SLIDESHOW_CUSTOM_IMAGE
        elif stream_enabled:
            image_path = STREAM_IMAGES[selection]
        else:
            image_path = APPS[selection]
        storage_bytes, storage_exact, storage_source = self.current_storage_usage()
        if storage_bytes > APP_SAFE_BYTES:
            messagebox.showerror(
                "Image exceeds safe app storage",
                f"{storage_source} uses {storage_bytes:,} bytes, which is "
                f"{storage_bytes - APP_SAFE_BYTES:,} bytes over the {APP_SAFE_BYTES:,}-byte app region.\n\n"
                "Deselect Launcher apps/photos or disable the screen streamer before flashing.",
            )
            return
        building_fresh_image = selection == "Launcher" or building_selected_photos or stream_enabled
        if not image_path.is_file() and not building_fresh_image:
            messagebox.showerror("Image not found", f"Cannot find:\n{image_path}\n\nBuild or copy the app image first.")
            return

        self.pending_steps.clear()
        pre_flash_steps: list[tuple[str, list[str], bool]] = []
        post_flash_steps: list[tuple[str, list[str], bool]] = []
        build_note = ""
        if selection == "Launcher":
            build_tool = LAUNCHER_BUILD_TOOL
            if not local_tool_available(build_tool):
                messagebox.showerror("Missing Launcher builder", f"Cannot find:\n{build_tool}")
                return
            command = local_tool_command(build_tool, ["--apps", *launcher_apps])
            if building_selected_photos:
                command.extend(["--photos", *(str(photo) for photo in self.slideshow_photos)])
            if stream_enabled:
                command.append("--screen-stream")
            build_note = (
                f"\nA fresh Launcher containing {len(launcher_apps)} app(s) will be built before flashing:\n"
                + ", ".join(launcher_apps)
                + "\n"
            )
            if building_selected_photos:
                build_note += f"It will include {len(self.slideshow_photos)} selected Slideshow photo(s).\n"
            if stream_enabled:
                build_note += "It will include the native 128×160 SWD screen stream.\n"
            pre_flash_steps.append(
                (
                    "Build Launcher: " + " + ".join(launcher_apps),
                    command,
                    False,
                )
            )
        elif building_selected_photos:
            build_tool = SLIDESHOW_BUILD_TOOL
            if not local_tool_available(build_tool):
                messagebox.showerror("Missing photo builder", f"Cannot find:\n{build_tool}")
                return
            build_note = (
                f"\n{len(self.slideshow_photos)} selected photo(s) will be built into a fresh Slideshow image before flashing.\n"
            )
            pre_flash_steps.append(
                (
                    "Build Slideshow with selected photos",
                    local_tool_command(
                        build_tool,
                        [
                            "--photos",
                            *(str(photo) for photo in self.slideshow_photos),
                            *(["--screen-stream"] if stream_enabled else []),
                        ],
                    ),
                    False,
                )
            )
        elif stream_enabled:
            if not local_tool_available(STREAM_BUILD_TOOL):
                messagebox.showerror("Missing stream-enabled app builder", f"Cannot find:\n{STREAM_BUILD_TOOL}")
                return
            build_note = (
                f"\nA fresh stream-enabled {selection} image will be built before flashing.\n"
                "The viewer reconstructs the native 128×160 RGB565 display stream.\n"
            )
            pre_flash_steps.append(
                (
                    f"Build stream-enabled {selection}",
                    local_tool_command(STREAM_BUILD_TOOL, ["--app", selection]),
                    False,
                )
            )
        if selection == "Launcher":
            level_percent = LAUNCHER_LEVEL_OPTIONS[self.launcher_level_var.get()]
            if level_percent is not None:
                post_flash_steps.append(
                    (
                        f"Set Launcher remaining-use display to {level_percent}%",
                        ["--set-launcher-level", str(level_percent)],
                        True,
                    )
                )
            profile = COIL_PROFILE_OPTIONS[self.coil_profile_var.get()]
            post_flash_steps.append((f"Set Launcher coil profile to {profile}", ["--set-coil-profile", profile], True))

        backup_note = "A backup will be created before the vape is erased.\n\n" if self.backup_before_flash_var.get() else ""
        config_note = ""
        if selection == "Launcher":
            config_note = (
                f"\nBundled apps: {' + '.join(launcher_apps)}\n"
                f"\nLauncher level: {self.launcher_level_var.get()}\n"
                f"Coil profile: {self.coil_profile_var.get()}\n"
            )
        if stream_enabled:
            config_note += "\nSWD screen streamer: included\n"
        storage_kind = "Exact" if storage_exact else "Projected"
        config_note += (
            f"\n{storage_kind} app storage: {storage_bytes:,} / {APP_SAFE_BYTES:,} bytes "
            f"({APP_SAFE_BYTES - storage_bytes:,} free; {SETTINGS_RESERVED_BYTES:,} settings bytes reserved)\n"
        )
        if not messagebox.askyesno(
            "Flash application?",
            f"{backup_note}Flash this image?\n\n{image_path.name}{build_note}{config_note}\n"
            "The app region will be erased and reprogrammed. The reserved 4 KB settings region is preserved.",
            icon="warning",
        ):
            return
        flash_step = (f"Flash {image_path.name}", ["--flash", str(image_path)], True)
        queued_steps = [*pre_flash_steps]
        if self.backup_before_flash_var.get():
            destination = self.make_backup_destination()
            if destination is None:
                return
            queued_steps.append(("Backup before flash", ["--backup", str(destination)], True))
        queued_steps.extend([flash_step, *post_flash_steps])
        self.pending_steps = queued_steps
        self.start_next_pending_step()


def main() -> int:
    if "--internal-tool" in sys.argv:
        index = sys.argv.index("--internal-tool")
        if index + 1 >= len(sys.argv):
            print("ERROR: missing internal tool name")
            return 2
        tool_name = sys.argv[index + 1]
        sys.argv = [tool_name, *sys.argv[index + 2 :]]
        try:
            if tool_name == "fast_flash":
                from fast_flash import main as tool_main
            elif tool_name == "build_launcher_with_photos":
                from build_launcher_with_photos import main as tool_main
            elif tool_name == "build_slideshow_with_photos":
                from build_slideshow_with_photos import main as tool_main
            elif tool_name == "build_streamable_app":
                from build_streamable_app import main as tool_main
            else:
                print(f"ERROR: unknown internal tool {tool_name}")
                return 2
            return tool_main()
        except (OSError, RuntimeError, TimeoutError) as exc:
            print(f"ERROR: {exc}")
            return 1
    if "--screen-streamer" in sys.argv:
        port = ""
        if "--port" in sys.argv:
            index = sys.argv.index("--port")
            if index + 1 < len(sys.argv):
                port = sys.argv[index + 1]
        from raz_screen_streamer import run_screen_streamer

        return run_screen_streamer(port)
    app = RazManager()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
