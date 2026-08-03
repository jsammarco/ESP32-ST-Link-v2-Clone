# Launcher

One firmware image containing a launcher menu, the Slideshow app, and Flappy Bird.
The launcher shares the normal Vaporware runtime, the three photos from
[`examples/photos`](../photos), and the existing Flappy game source. It does not
modify `examples/flappy` or `examples/Slideshow`.

## Controls

| Screen | Gesture | Action |
| --- | --- | --- |
| Menu | Tap | Switch highlighted app |
| Menu | Hold 650 ms, then release | Start highlighted app |
| Slideshow | Tap | Next photo |
| Slideshow | Double-tap | Switch Normal / Boost output mode |
| Slideshow | Triple-tap | Return to menu |
| Slideshow | Draw from mouthpiece | Fire selected mode, subject to its cutoff |
| Slideshow | Hold 650 ms | Button fallback for the selected mode, subject to its cutoff |
| Flappy | Standard button presses | Flap / game controls |
| Flappy | Hold 2 s, then release | Return to menu |

Every launcher transition turns the coil output off first. The embedded Flappy code
keeps its current score-triggered coil behaviour, so its own safeguards and controls
remain unchanged.

## Slideshow coil control

The factory's actual draw input is PA3, active-high: its firmware stops the heater
when PA3 goes low. The Slideshow app configures PA3 only as an input, requires an
idle-low observation followed by two high samples before starting, stops immediately
when PA3 goes low, and latches the hard cutoff until the draw is released. The button
hold remains a fallback. The launcher menu never requests coil output.

`Normal` uses 50% frame-duty output with a 1.8 s hard cutoff; `Boost` uses continuous
output with a 0.9 s hard cutoff.

## Battery status

The menu takes five PA6 ADC readings per update, uses their median, and applies a
small smoothing filter before mapping the result through a single-cell Li-ion voltage
curve. It shows both the estimated percentage and the PA6-derived cell voltage (for
example, `4.08V`), so the estimate can be checked against the measured voltage rather
than treated as an exact fuel gauge. This avoids the old linear 2.5–3.7 V scale, which
made normal battery discharge look jumpy.

The Launcher enters **LOW BATTERY** at approximately 3.40 V (or immediately at
approximately 3.30 V), dims the PB4 backlight to 20% using 1 kHz PWM, exits any
running app to the warning screen, and locks the coil OFF. The lock applies to both
the Slideshow and embedded Flappy code, so no app can energize the coil in this mode.
After charging, it requires three consecutive filtered readings around 3.53 V or
higher before restoring normal brightness and coil operation. These are conservative
guard bands; actual voltage varies with cell condition, temperature, and load.

`CHARGING` is a direct active-low signal from PB1, matching the input and internal
pull-up configuration in `firmware/MyWhiteRAZ_backup.bin`. The Launcher requires
three consecutive 100 ms samples before changing the state, so connector noise does
not flicker the label. It shows while the charge controller reports active charging;
a USB-connected but fully charged device may instead show `CHARGE IDLE`.

The Launcher's embedded Slideshow has a readable status band above the photo. It
shows `BATT`, percentage, measured voltage, and either `CHARGING` or `CHARGE IDLE`.
It uses the same filtered state as the menu; green is 60% or higher, yellow is
25-59%, orange is below 25%, and red means the low-battery lockout is active.

## Vape remaining level

The launcher also tracks powered-heater time and shows a six-segment `VAPE` gauge.
Its scale matches the reverse-engineered factory `MyBlueRAZ_backup.bin`: one segment
per 60,000 centisecond ticks (10 minutes), empty at 340,000 ticks (56 minutes 40
seconds). The counter is stored in the Launcher's internal-flash NV area, so routine
Launcher reflashes preserve it.

`extract_factory_vape_level.py` documents this distinction and decodes the original
value from a raw external-flash dump when one is available:

```cmd
python extract_factory_vape_level.py ..\..\firmware\MyBlueRAZ_backup.bin
python extract_factory_vape_level.py external_flash_dump.bin
```

If the vape is connected to the ST-Link, use the targeted read-only tool instead
of dumping the entire external chip:

```cmd
read_factory_vape_level.bat
```

It prints `FACTORY_VAPE_TICKS`, `FACTORY_VAPE_PERCENT`, and the six-bar level.
It also prints the external flash's JEDEC ID. The tool only reads the five-byte
factory record, saves `factory_vape_level.json`, and then resumes the launcher; it
does not write to vendor external flash or flash firmware. `build_launcher.bat`
converts that JSON into a one-time import seed. The imported factory value replaces
any old Launcher tracker value only when the JSON value changes; later flashes keep
the Launcher’s accumulated usage.

## Build and flash

```cmd
build_launcher.bat
flash_vape.bat
```

`build_launcher.bat` runs the shared photo converter, producing
`generated\photos.h` from the three source images. The generated image data and all
build/flash output are ignored by Git. The default three-photo configuration was
chosen to leave headroom in the N32G031's 60 KB application flash region.

For a one-off Launcher image with up to three selected embedded-Slideshow photos,
use the desktop GUI's **Choose up to 3 photos...** button while **Launcher** is
selected. It builds `build\launcher-photos.bin` separately, then restores the normal
generated image header, so the bundled `launcher.bin` is not replaced.

The flasher is compatible with an ST-Link already attached through `usbipd`; set
`STLINK_BUSID` near the top of `flash_vape.bat` if your adapter uses a different ID.
