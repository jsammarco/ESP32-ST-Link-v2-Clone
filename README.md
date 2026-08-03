# ESP32 DevKit V1 SWD adapter for RAZ vape hardware

An original ESP32 DevKit V1 can act as a low-cost SWD programmer for the
Nations N32G031 MCU used in some RAZ DC25000 devices. The ESP32 does **not**
enumerate as a real USB ST-Link V2—the DevKit USB connector is a CP210x/CH340
USB-to-UART bridge. Instead, this project sends the firmware image to the
ESP32 over its COM port; the ESP32 then performs SWD locally over two GPIO pins.

The supported flashing path is entirely wired: no Wi-Fi, LAN, USB/IP, or
physical ST-Link is required.

> **Experimental hardware project — use only on hardware you own.** A vape is
> a battery-powered, heating device. Incorrect wiring or firmware can erase the
> application, prevent normal operation, damage the device, or create a fire
> hazard. Work on a non-flammable surface, do not connect the ESP32 to the
> device's power rails, and do not leave a modified device unattended.

## What is included

- ESP32 DevKit V1 firmware: local SWD probe, erase, program, verification, and
  target reset for the N32G031.
- `tools/fast_flash.py`: the recommended Windows serial flasher.
- `tools/raz_manager_gui.py`: a local desktop GUI for connection testing,
  saved-value reads, backup, restore, and bundled-app flashing.
- An optional OpenOCD `remote_bitbang` bridge for diagnostics and read-only
  troubleshooting.
- Prebuilt RAZ application images in [`RAZ Vape Apps`](<RAZ Vape Apps>):

| App | Ready-to-flash image | Notes |
|---|---|---|
| Launcher | [`launcher.bin`](<RAZ Vape Apps/Launcher/build/launcher.bin>) | Configurable menu containing one or two selected apps. |
| Tetris | [`tetris.bin`](<RAZ Vape Apps/Tetris/build/tetris.bin>) | Standalone one-button Tetris game. |
| Slideshow | [`slideshow.bin`](<RAZ Vape Apps/Slideshow/build/slideshow.bin>) | Photo slideshow. Its source includes limited coil-output modes; review it carefully before use. |
| Flappy | [`flappy.bin`](<RAZ Vape Apps/flappy/build/flappy.bin>) | Flappy-style application for the vape display. |

The source snapshots are included for reference. They derive from the
[ImoverEngineering/Vaporware](https://github.com/ImoverEngineering/Vaporware)
SDK; their `build_*.bat` files expect that larger SDK tree (shared
`src/include`, linker script, and runtime) and are **not** standalone rebuilds
in this repository. The prebuilt `.bin` files above are the supported images to
flash from this clone.

## Hardware

### Required

- ESP32 DevKit V1 with its normal USB-to-UART connection
- USB-C **male** breakout board
- Three short jumper wires
- 100 ohm series resistors for SWDIO and SWCLK
- A Windows PC with Python and PlatformIO

### Wiring

On the RAZ hardware, the USB-C CC contacts are used as SWD pins:

| ESP32 DevKit V1 | USB-C male breakout | Signal |
|---|---|---|
| `GPIO25` through 100 ohm | `CC1` | SWCLK on attempt 1; SWDIO on attempt 2 |
| `GPIO26` through 100 ohm | `CC2` | SWDIO on attempt 1; SWCLK on attempt 2 |
| `GND` | `GND` | Common ground |

Do **not** connect ESP32 `3V3`, `5V`, `VBUS`, `D+`, or `D-` to the vape. The
target powers its own MCU. `GPIO27` is reserved for optional reset support and
is not required.

The current ESP32 firmware tries the two possible assignments automatically for
every direct probe/flash/backup/restore operation:

1. `SWDIO = GPIO26`, `SWCLK = GPIO25`
2. `SWDIO = GPIO25`, `SWCLK = GPIO26` (only if attempt 1 cannot read DPIDR)

Keep the two wires connected as shown; do not manually swap CC1/CC2 between
attempts. The optional OpenOCD serial bridge also runs the same read-only
auto-probe when OpenOCD connects. If both mappings fail, verify common ground,
wake the vape, and keep the wires short.

## Desktop GUI

After uploading the current ESP32 firmware, launch the local GUI from the
repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\start_raz_manager.ps1
```

Select the ESP32's COM port in the window. The GUI excludes `COM1` from its
port list because that is normally the legacy motherboard serial port. It can
test the SWD connection, create a named (or timestamped) backup, restore a
selected backup, flash Launcher/Tetris/Slideshow/Flappy or a custom `.bin`, and display the persisted
internal-flash values. It runs the same `fast_flash.py` protocol as the
command-line workflow and its log shows the exact ESP32 progress messages.

When **Launcher** is selected, choose one or two bundled apps from the two
**Launcher bundle** boxes. The available modules are Tetris, Flappy, and
Slideshow; choose **None** in the second box for a one-app launcher. The Manager
builds `launcher-custom.bin` from that selection before it backs up and flashes.

When standalone **Slideshow** is selected, or Launcher includes a Slideshow
slot, use **Choose up to 3 photos...** to select one to three `.bmp`, `.gif`,
`.jpeg`, `.jpg`, `.png`, or `.webp` files. On Flash, the manager first builds a
new image containing those photos, then performs the optional backup and flashes
that freshly built image. For Launcher the custom result is `launcher-custom.bin`;
the normal `launcher.bin` remains unchanged. Likewise,
a standalone Slideshow selection creates `slideshow-photos.bin` without
replacing `slideshow.bin`. Clearing the selection returns to the bundled app
image. The originals are only copied into a temporary build folder, and the
generated source image header is restored afterward. Images are centre-cropped
and resized for the 128×160 display, then quantized to 16 colours. This
requires Pillow (`py -m pip install pillow`) and a Vaporware SDK checkout; the normal sibling path
`C:\Users\Joe\Projects\Vaporware\src` is detected automatically, or set
`VAPORWARE_SDK` to the SDK root (or its `src` folder).

Use **Update ESP32 firmware...** whenever this repository's `src/main.cpp`
changes. The GUI locates PlatformIO (including the standard
`~\.platformio\penv\Scripts\platformio.exe` installation), uploads to the
selected ESP32 port, and does not communicate with or alter the vape during
that operation.

### Launcher pre-flash options

When **Launcher** is selected, the GUI also presents settings that are applied
after the image verifies. **Create a backup before flashing** is enabled by
default. The Launcher-only options are deliberately bounded:

| Option | Default | Effect |
|---|---|---|
| Remaining-use display | Preserve saved value | `100%` sets the Launcher's internal use tracker to zero. It changes only the displayed tracker; it does not recharge the battery or consumable. |
| Coil profile | Current app default | Preserves the existing Normal (50% duty, 1.8 s cutoff) and Boost (continuous, 0.9 s cutoff) behavior. |
| Conservative coil profile | Not selected | Uses lower duty cycles and shorter cutoffs: Normal 33% / 1.5 s and Boost 50% / 0.7 s. |
| Coil disabled | Not selected | Keeps the Launcher UI usable but prevents coil drive. |

No GUI option increases the existing output duty cycle or cutoff time. The
profile setting only applies to the updated bundled Launcher image; it is
disabled for Slideshow, Flappy, and custom binaries.

**Get saved vape values** is read-only. It briefly halts the target to inspect
the final 4 KB internal-flash key/value area, then resumes it. Values have the
following app-specific meaning:

| Displayed value | Internal key | Meaning |
|---|---:|---|
| Puff count | 0 | Available only from apps that save a discrete puff count. |
| Total vape time | 1 | Available only from apps that use this timer. |
| Launcher heater use | 5 | The Launcher's powered-heater time in 0.01-second ticks, plus its derived six-bar remaining gauge. |
| Launcher factory import | 6 | Marker used by the Launcher when importing a factory level seed. |
| Game values | 2-4 | Flappy high score and slot-machine values when those apps saved them. |

The bundled Launcher does **not** track individual puffs; it records
powered-heater time in key 5. Therefore its puff-count entry normally shows
`not stored`, which is expected rather than an error.

## Quick start: flash a bundled app

The commands below use `COM7` as an example. Replace it with the ESP32's COM
port shown in Device Manager. Ensure no serial monitor or
`tools/serial_bridge.py` process is using that port.

### 1. Install host prerequisites

Install [PlatformIO](https://platformio.org/install) and Python 3, then install
the one Python dependency:

```powershell
python -m pip install pyserial
```

### 2. Upload the ESP32 firmware

```powershell
cd "C:\path\to\ESP32-ST-Link-v2-Clone"
pio run --target upload --upload-port COM7
```

If `pio` is not on your `PATH`, use the PlatformIO executable from your local
installation instead.

### 3. Confirm the SWD connection (read-only)

With the breakout connected and the target powered, run:

```powershell
python tools\fast_flash.py --port COM7 --probe
```

Expected output includes:

```text
IDR 0BB11477
```

This test only reads the target debug-port ID; it does not erase or program
flash.

### 4. Flash an application

For Launcher:

```powershell
python tools\fast_flash.py --port COM7 --flash ".\RAZ Vape Apps\Launcher\build\launcher.bin"
```

For Tetris, Slideshow, or Flappy, replace the image path with the corresponding `.bin`
listed above. The flasher displays `ERASE`, `PROGRAM`, and `VERIFY` progress.
Only `DONE` means the image verified and the ESP32 requested a target reset.

Do not disconnect the wiring or interrupt the tool once erasing begins. An
interrupted operation can leave the target without valid application firmware.

## Back up and restore internal flash

Back up a working device **before** experimenting with firmware. The backup
tool halts the MCU briefly, reads all 64 KB of internal flash plus an 8 KB RAM
snapshot, saves SHA-256 hashes in `manifest.json`, and then resumes the target.
The default destination is a timestamped directory under `backups/`, which is
ignored by Git because it can contain device-specific data.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\backup_raz.ps1 -Port COM7
```

A completed backup contains:

| File | Contents | Restore behavior |
|---|---|---|
| `internal_flash.bin` | Entire 64 KB N32G031 internal flash, including the final 4 KB persistent NV region | Restored and verified |
| `ram_snapshot.bin` | 8 KB live RAM capture at the moment the CPU was halted | Archive/diagnostic only; not restored |
| `manifest.json` | Timestamp, DPIDR, sizes, and SHA-256 hashes | Validated before restore |

The full internal-flash image preserves data stored in its persistent NV area,
including the Launcher’s heater-use/settings state and any app-provided puff
count. RAM cannot be preserved
across a reset or power loss, so its snapshot is deliberately not written back.
Data held in an external flash chip or other peripherals is also outside this
backup’s scope.

Restore only a backup you trust. This erases all 64 KB of internal flash,
including the current application and persistent settings:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\restore_raz.ps1 `
  -Port COM7 `
  -Backup ".\backups\raz-YYYYMMDD-HHMMSS" `
  -Confirm
```

The restore tool validates `manifest.json` when present, programs the complete
flash image, verifies it, and requests a target reset only after success.

You can also restore from `internal_flash.bin` by itself; the folder, RAM
snapshot, and manifest are not required. The file must be an exact 65,536-byte
full internal-flash image. The image is still verified by the ESP32 after it is
written, but there is no pre-restore SHA-256 manifest check when the `.bin` is
used alone:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\restore_raz.ps1 `
  -Port COM7 `
  -Backup "C:\path\to\internal_flash.bin" `
  -Confirm
```

In the desktop manager, choose **Restore backup / .bin...**, select **Yes** for
a standalone file, then choose the 64 KB `.bin` image.

## Launcher controls

Launcher contains the one or two apps selected in the Manager:

| Screen | Gesture | Action |
|---|---|---|
| Menu | Tap | Change selection |
| Menu | Hold about 650 ms, then release | Start the selected app |
| Tetris | One tap / two taps / 450 ms hold | Right / left / rotate |
| Tetris | Pressure-sensor draw | Hard-drop; coil is permitted only for 1.5 seconds from a row clear |
| Tetris | Hold about 2 seconds, then release | Return to menu |
| Slideshow | Tap | Next photo |
| Slideshow | Triple-tap | Return to menu |
| Flappy | Hold about 2 seconds, then release | Return to menu |

See the app-specific documentation in
[`RAZ Vape Apps/Launcher`](<RAZ Vape Apps/Launcher>) and
[`RAZ Vape Apps/Slideshow`](<RAZ Vape Apps/Slideshow>) before enabling or
modifying any coil-control behavior.

## Optional OpenOCD bridge

`tools/serial_bridge.py`, `n32g031-esp32.openocd.cfg`, and the OpenOCD build
helpers are retained for diagnostics and experimentation with OpenOCD's
`remote_bitbang` SWD driver. They are not required for normal fast flashing.

Use the direct `fast_flash.py` workflow above for bundled firmware: it avoids
the per-bit USB serial round trips that make OpenOCD programming very slow.

## Troubleshooting

| Symptom | What to check |
|---|---|
| `Access is denied` for `COM7` | Close PlatformIO Monitor, the serial bridge, and any other serial program. |
| `ERR PROBE` or no `IDR` response | Both GPIO mappings were tried. Verify GND, wake the target, and keep the CC wires short. |
| `VERIFY_FAIL` or `ERR FLASH` | Keep the wires short, retain the 100 ohm resistors, charge the device, and rerun the read-only probe before retrying. |
| Screen does not return immediately after `DONE` | Verify the latest ESP32 firmware was uploaded, then power-cycle/reset the target before attempting another flash. |

## Project status

This is a hardware-specific experimental project for the N32G031-based RAZ
layout tested by its author. Other RAZ models, ESP32 board variants, USB-C
breakouts, and pin assignments may differ.

## Reference

- [ImoverEngineering/Vaporware](https://github.com/ImoverEngineering/Vaporware)
  — upstream SDK and source reference for the bundled RAZ app snapshots.
