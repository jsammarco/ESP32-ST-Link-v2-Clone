#!/usr/bin/env python3
"""Windows desktop manager for the ESP32 RAZ SWD adapter.

This is a graphical wrapper around fast_flash.py. It keeps the serial protocol
in one well-tested command-line tool while running long operations off the UI
thread and showing the exact ESP32 progress messages in the window.
"""

from __future__ import annotations

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

try:
    from serial.tools import list_ports
except ModuleNotFoundError:
    list_ports = None


REPO_ROOT = Path(__file__).resolve().parent.parent
FLASH_TOOL = Path(__file__).resolve().with_name("fast_flash.py")
SLIDESHOW_BUILD_TOOL = Path(__file__).resolve().with_name("build_slideshow_with_photos.py")
LAUNCHER_BUILD_TOOL = Path(__file__).resolve().with_name("build_launcher_with_photos.py")
BACKUP_ROOT = REPO_ROOT / "backups"
NV_LINE = re.compile(r"^NV\s+(\d+)\s+(NONE|[0-9A-Fa-f]{8})$")

APPS = {
    "Launcher": REPO_ROOT / "RAZ Vape Apps" / "Launcher" / "build" / "launcher.bin",
    "Slideshow": REPO_ROOT / "RAZ Vape Apps" / "Slideshow" / "build" / "slideshow.bin",
    "Flappy": REPO_ROOT / "RAZ Vape Apps" / "flappy" / "build" / "flappy.bin",
}
SLIDESHOW_CUSTOM_IMAGE = REPO_ROOT / "RAZ Vape Apps" / "Slideshow" / "build" / "slideshow-photos.bin"
LAUNCHER_CUSTOM_IMAGE = REPO_ROOT / "RAZ Vape Apps" / "Launcher" / "build" / "launcher-photos.bin"

NV_LABELS = {
    0: "Puff count",
    1: "Total vape time",
    2: "Flappy high score",
    3: "Slot spins",
    4: "Slot wins",
    5: "Launcher heater use",
    6: "Launcher factory import",
    7: "App 2 value",
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


class RazManager(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("RAZ ESP32 Manager")
        self.minsize(760, 990)
        self.geometry("860x1140")
        self.option_add("*tearOff", False)

        self.events: queue.Queue[tuple[str, object]] = queue.Queue()
        self.process: subprocess.Popen[str] | None = None
        self.current_action = ""
        self.values: dict[int, int | None] = {}
        # A queued action is (label, command arguments, uses_fast_flash_tool).
        # Build steps run as ordinary local commands; hardware steps use the
        # selected ESP32 serial port through fast_flash.py.
        self.pending_steps: list[tuple[str, list[str], bool]] = []
        self.port_var = tk.StringVar()
        self.app_var = tk.StringVar(value="Launcher")
        self.backup_name_var = tk.StringVar()
        self.backup_before_flash_var = tk.BooleanVar(value=True)
        self.launcher_level_var = tk.StringVar(value="Preserve saved value")
        self.coil_profile_var = tk.StringVar(value="Current app default")
        self.custom_image_path: Path | None = None
        self.custom_image_var = tk.StringVar(value="Bundled Launcher image selected.")
        self.slideshow_photos: list[Path] = []
        self.slideshow_photo_var = tk.StringVar(
            value="No custom embedded Slideshow photos selected — the bundled app image will be used."
        )
        self.status_var = tk.StringVar(value="Choose the ESP32 COM port, then use a read-only action first.")
        self.value_text_var = tk.StringVar(value="No values read yet.")
        self.action_widgets: list[tk.Widget] = []

        self._build_ui()
        self.refresh_ports()
        self.after(75, self._drain_events)

    def _build_ui(self) -> None:
        main = ttk.Frame(self, padding=14)
        main.grid(sticky="nsew")
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)
        main.columnconfigure(1, weight=1)
        main.columnconfigure(3, weight=0)
        main.rowconfigure(5, weight=1)

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

        read_frame = ttk.LabelFrame(main, text="Read only", padding=10)
        read_frame.grid(row=3, column=0, columnspan=4, sticky="ew", pady=(14, 0))
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
        write_frame.grid(row=4, column=0, columnspan=4, sticky="ew", pady=(12, 0))
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
        restore = ttk.Button(write_frame, text="Restore backup...", command=self.restore)
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
        ttk.Label(write_frame, text="Embedded photos:").grid(row=5, column=0, sticky="w", pady=(10, 0))
        self.choose_slideshow_photos_button = ttk.Button(
            write_frame,
            text="Choose up to 3 photos...",
            command=self.choose_slideshow_photos,
        )
        self.choose_slideshow_photos_button.grid(row=5, column=1, sticky="w", padx=(8, 0), pady=(10, 0))
        self.clear_slideshow_photos_button = ttk.Button(
            write_frame,
            text="Clear photos",
            command=self.clear_slideshow_photos,
        )
        self.clear_slideshow_photos_button.grid(row=5, column=2, sticky="w", padx=(8, 0), pady=(10, 0))
        self.action_widgets.extend([self.choose_slideshow_photos_button, self.clear_slideshow_photos_button])
        ttk.Label(write_frame, textvariable=self.slideshow_photo_var, foreground="#444444", wraplength=610).grid(
            row=6, column=1, columnspan=3, sticky="w", padx=(8, 0), pady=(3, 0)
        )
        backup_before_flash = ttk.Checkbutton(
            write_frame,
            text="Create a backup before flashing",
            variable=self.backup_before_flash_var,
        )
        backup_before_flash.grid(row=7, column=1, columnspan=2, sticky="w", padx=(8, 0), pady=(10, 0))
        self.action_widgets.append(backup_before_flash)

        ttk.Label(write_frame, text="Launcher level:").grid(row=8, column=0, sticky="w", pady=(10, 0))
        self.launcher_level_box = ttk.Combobox(
            write_frame,
            textvariable=self.launcher_level_var,
            values=list(LAUNCHER_LEVEL_OPTIONS),
            state="readonly",
            width=25,
        )
        self.launcher_level_box.grid(row=8, column=1, sticky="w", padx=(8, 0), pady=(10, 0))

        ttk.Label(write_frame, text="Coil profile:").grid(row=9, column=0, sticky="w", pady=(6, 0))
        self.coil_profile_box = ttk.Combobox(
            write_frame,
            textvariable=self.coil_profile_var,
            values=list(COIL_PROFILE_OPTIONS),
            state="readonly",
            width=29,
        )
        self.coil_profile_box.grid(row=9, column=1, sticky="w", padx=(8, 0), pady=(6, 0))
        self.action_widgets.extend([self.launcher_level_box, self.coil_profile_box])
        ttk.Label(
            write_frame,
            text=("Launcher-only options. ‘100%’ resets its on-screen remaining-use tracker; it does not recharge "
                  "the battery or consumable. No profile increases the current app's output or cutoff."),
            foreground="#444444",
            wraplength=650,
        ).grid(row=10, column=1, columnspan=3, sticky="w", padx=(8, 0), pady=(4, 0))
        ttk.Label(
            write_frame,
            text="Always create a backup before flashing or restoring. Restore overwrites all internal flash and saved settings.",
            foreground="#7a3000",
            wraplength=650,
        ).grid(row=11, column=0, columnspan=4, sticky="w", pady=(10, 0))

        values_frame = ttk.LabelFrame(main, text="Saved values", padding=10)
        values_frame.grid(row=5, column=0, columnspan=4, sticky="new", pady=(12, 0))
        ttk.Label(values_frame, textvariable=self.value_text_var, justify="left", wraplength=760).grid(sticky="w")

        log_frame = ttk.LabelFrame(main, text="Operation log", padding=8)
        log_frame.grid(row=6, column=0, columnspan=4, sticky="nsew", pady=(12, 0))
        main.rowconfigure(6, weight=1)
        self.log = scrolledtext.ScrolledText(log_frame, height=8, wrap="word", state="disabled", font=("Cascadia Mono", 9))
        self.log.grid(sticky="nsew")
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)

        status = ttk.Label(main, textvariable=self.status_var, relief="sunken", anchor="w")
        status.grid(row=7, column=0, columnspan=4, sticky="ew", pady=(12, 0))

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
        selection = self.app_var.get()
        if selection == CUSTOM_APP:
            self.choose_custom_image()
        else:
            self.custom_image_var.set(f"Bundled image: {APPS[selection]}")
        self.update_launcher_options()
        self.update_slideshow_photo_options()

    def update_launcher_options(self) -> None:
        state = "readonly" if self.app_var.get() == "Launcher" and self.process is None else "disabled"
        self.launcher_level_box.configure(state=state)
        self.coil_profile_box.configure(state=state)

    def update_slideshow_photo_options(self) -> None:
        state = "normal" if self.app_var.get() in {"Slideshow", "Launcher"} and self.process is None else "disabled"
        self.choose_slideshow_photos_button.configure(state=state)
        self.clear_slideshow_photos_button.configure(state=state)

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
        names = ", ".join(path.name for path in photos)
        self.slideshow_photo_var.set(f"{len(photos)} photo(s) selected: {names}")
        target = "Launcher" if self.app_var.get() == "Launcher" else "Slideshow"
        self.status_var.set(f"Selected photos will be embedded when {target} is flashed.")

    def clear_slideshow_photos(self) -> None:
        self.slideshow_photos.clear()
        self.slideshow_photo_var.set(
            "No custom embedded Slideshow photos selected — the bundled app image will be used."
        )
        self.status_var.set("Custom embedded Slideshow photo selection cleared.")

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
        self.custom_image_var.set(f"Custom image: {self.custom_image_path}")
        self.status_var.set(f"Custom firmware selected: {self.custom_image_path.name}")
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

    def run_tool(self, action: str, arguments: list[str]) -> None:
        port = self.selected_port()
        if port is None or self.process is not None:
            return
        if not FLASH_TOOL.is_file():
            messagebox.showerror("Missing tool", f"Cannot find:\n{FLASH_TOOL}")
            return

        command = [sys.executable, "-u", str(FLASH_TOOL), "--port", port, *arguments]
        self.run_command(action, command)

    def run_command(self, action: str, command: list[str]) -> None:
        if self.process is not None:
            return
        self.append_log("\n> " + subprocess.list2cmdline(command))
        self.status_var.set(f"{action} is running. Do not unplug the ESP32 or target.")
        self.current_action = action
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
        threading.Thread(target=self._read_process, args=(self.process,), daemon=True).start()

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
            "This uploads the current ESP32 SWD-adapter firmware to the selected COM port.\n\n"
            "It does not flash or erase the vape. The ESP32 will restart after the upload.\n\n"
            f"Update the ESP32 on {port}?",
            icon="warning",
        ):
            return
        self.run_command(
            "Update ESP32 firmware",
            [platformio, "run", "--target", "upload", "--upload-port", port],
        )

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
        backup = filedialog.askdirectory(title="Choose a RAZ backup folder", initialdir=initial)
        if not backup:
            return
        if not (Path(backup) / "internal_flash.bin").is_file():
            messagebox.showerror("Invalid backup", "Choose a backup folder containing internal_flash.bin.")
            return
        if not messagebox.askyesno(
            "Restore full backup?",
            "This erases and replaces all 64 KB of internal flash, including the installed app and saved settings.\n\n"
            "Restore the selected backup now?",
            icon="warning",
        ):
            return
        self.run_tool("Restore", ["--restore", backup, "--confirm-restore"])

    def flash_app(self) -> None:
        if self.selected_port() is None or self.process is not None:
            return
        selection = self.app_var.get()
        building_selected_photos = selection in {"Slideshow", "Launcher"} and bool(self.slideshow_photos)
        if selection == CUSTOM_APP:
            if self.custom_image_path is None or not self.custom_image_path.is_file():
                if self.choose_custom_image() is None:
                    return
            image_path = self.custom_image_path
            if image_path is None:
                return
        else:
            if building_selected_photos:
                image_path = LAUNCHER_CUSTOM_IMAGE if selection == "Launcher" else SLIDESHOW_CUSTOM_IMAGE
            else:
                image_path = APPS[selection]
        if not image_path.is_file() and not building_selected_photos:
            messagebox.showerror("Image not found", f"Cannot find:\n{image_path}\n\nBuild or copy the app image first.")
            return

        self.pending_steps.clear()
        pre_flash_steps: list[tuple[str, list[str], bool]] = []
        post_flash_steps: list[tuple[str, list[str], bool]] = []
        photo_build_note = ""
        if building_selected_photos:
            build_tool = LAUNCHER_BUILD_TOOL if selection == "Launcher" else SLIDESHOW_BUILD_TOOL
            target_name = "Launcher (embedded Slideshow)" if selection == "Launcher" else "Slideshow"
            if not build_tool.is_file():
                messagebox.showerror("Missing photo builder", f"Cannot find:\n{build_tool}")
                return
            photo_build_note = (
                f"\n{len(self.slideshow_photos)} selected photo(s) will be built into a fresh {target_name} image before flashing.\n"
            )
            pre_flash_steps.append(
                (
                    f"Build {target_name} with selected photos",
                    [
                        sys.executable,
                        "-u",
                        str(build_tool),
                        "--photos",
                        *(str(photo) for photo in self.slideshow_photos),
                    ],
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
                f"\nLauncher level: {self.launcher_level_var.get()}\n"
                f"Coil profile: {self.coil_profile_var.get()}\n"
            )
        if not messagebox.askyesno(
            "Flash application?",
            f"{backup_note}Flash this image?\n\n{image_path.name}{photo_build_note}{config_note}\n"
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
    app = RazManager()
    app.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
