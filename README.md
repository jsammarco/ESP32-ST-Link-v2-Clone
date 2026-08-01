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
- An optional OpenOCD `remote_bitbang` bridge for diagnostics and read-only
  troubleshooting.
- Prebuilt RAZ application images in [`RAZ Vape Apps`](<RAZ Vape Apps>):

| App | Ready-to-flash image | Notes |
|---|---|---|
| Launcher | [`launcher.bin`](<RAZ Vape Apps/Launcher/build/launcher.bin>) | Menu containing the embedded Slideshow and Flappy apps. |
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
| `GPIO25` through 100 ohm | `CC1` | SWDIO |
| `GPIO26` through 100 ohm | `CC2` | SWCLK |
| `GND` | `GND` | Common ground |

Do **not** connect ESP32 `3V3`, `5V`, `VBUS`, `D+`, or `D-` to the vape. The
target powers its own MCU. `GPIO27` is reserved for optional reset support and
is not required.

If the probe cannot read the target, verify the common ground, wake the vape,
and swap only the CC1/CC2 wires. Keep the wires short.

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

For Slideshow or Flappy, replace the image path with the corresponding `.bin`
listed above. The flasher displays `ERASE`, `PROGRAM`, and `VERIFY` progress.
Only `DONE` means the image verified and the ESP32 requested a target reset.

Do not disconnect the wiring or interrupt the tool once erasing begins. An
interrupted operation can leave the target without valid application firmware.

## Launcher controls

The bundled Launcher combines Slideshow and Flappy in one image:

| Screen | Gesture | Action |
|---|---|---|
| Menu | Tap | Change selection |
| Menu | Hold about 650 ms, then release | Start the selected app |
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
| `ERR PROBE` or no `IDR` response | Verify GND, wake the target, then swap CC1 and CC2. |
| `VERIFY_FAIL` or `ERR FLASH` | Keep the wires short, retain the 100 ohm resistors, charge the device, and rerun the read-only probe before retrying. |
| Screen does not return immediately after `DONE` | Verify the latest ESP32 firmware was uploaded, then power-cycle/reset the target before attempting another flash. |

## Project status

This is a hardware-specific experimental project for the N32G031-based RAZ
layout tested by its author. Other RAZ models, ESP32 board variants, USB-C
breakouts, and pin assignments may differ.

## Reference

- [ImoverEngineering/Vaporware](https://github.com/ImoverEngineering/Vaporware)
  — upstream SDK and source reference for the bundled RAZ app snapshots.
