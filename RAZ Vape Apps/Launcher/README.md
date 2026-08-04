# Launcher

One firmware image containing a launcher menu and one or more build-selected apps.
Tetris, Pac-Man, Mario 1-1, Geometry Dash, Chrome Dino, Tower Stacker, Doom,
Flappy Bird, and Slideshow can be included in any combination that fits. The
RAZ ESP32 Manager builds a fresh image from its Launcher bundle checkboxes
before flashing. Its Launcher title box sets the heading (default
`ConsultingJoe.com`) for that build.

## Controls

| Screen | Gesture | Action |
| --- | --- | --- |
| Menu | Tap | Switch highlighted app |
| Menu | Hold 650 ms, then release | Start highlighted app |
| Tetris | One short press | Move right after the double-tap window |
| Tetris | Two short presses | Move left |
| Tetris | Hold 450 ms | Rotate clockwise |
| Tetris | Draw from pressure (puff) sensor | Taking a puff hard-drops and locks the active piece |
| Tetris | Hold 2 s, then release | Return to menu |
| Pac-Man | One short press | Queue a right turn immediately on release |
| Pac-Man | Two short presses | Queue a left turn |
| Pac-Man | Hold 450 ms | Reverse direction |
| Pac-Man | Hold 2 s, then release | Return to menu |
| Mario 1-1 | One short press | Toggle running forward on or off |
| Mario 1-1 | Two short presses within 260 ms | Step backward two tiles without scrolling to a completed screen |
| Mario 1-1 | Hold 420 ms | Jump once |
| Mario 1-1 | Hold 2 s, then release | Return to menu |
| Geometry Dash | Press | Start; while running, reverse gravity between floor and ceiling |
| Geometry Dash | Hold 2 s, then release | Return to menu |
| Chrome Dino | Press | Start, jump from the ground, or retry |
| Chrome Dino | Hold 2 s, then release | Return to menu |
| Tower Stacker | Press | Start, drop the moving floor, or retry after a miss |
| Tower Stacker | Hold 2 s, then release | Return to menu |
| Doom | One tap / two taps | Turn right after a 0.3 s gesture window / turn left without an intermediate camera jump |
| Doom | Hold about 0.15 seconds | Walk forward; keep holding for responsive continuous movement |
| Doom | Draw from mouthpiece | Fire with bounded Normal coil output |
| Doom | Hold 2 s, then release | Return to menu |
| Slideshow | Tap | Next photo |
| Slideshow | Double-tap | Switch Normal / Boost output mode |
| Slideshow | Triple-tap | Return to menu |
| Slideshow | Draw from mouthpiece | Fire selected mode, subject to its cutoff |
| Slideshow | Hold 650 ms | Button fallback for the selected mode, subject to its cutoff |
| Flappy | Standard button presses | Flap / game controls |
| Flappy | Hold 2 s, then release | Return to menu |

Every launcher transition turns the coil output off first. Pac-Man never requests
coil output. Tetris never requests coil output during ordinary play. Clearing a
row opens a 1.5-second `VAPE NOW` window measured from the clear; only an active
pressure-sensor draw inside that window can enable it, and release or timeout
stops it immediately. The embedded Doom and Flappy code keep their existing
bounded coil behaviour, with coil requests routed through the Launcher's
low-battery interlock and remaining-level accounting.

Pac-Man uses the canonical 28x31 arcade maze and moves one tile every 480 ms.
Requested turns stay buffered until legal; unambiguous corners and dead-end
reversals are automatic to make the relative controls practical with one button.

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
running app to the warning screen, and locks the coil OFF. The lock applies to every
bundled module, so no app can energize the coil in this mode.
After charging, it requires three consecutive filtered readings around 3.53 V or
higher before restoring normal brightness and coil operation. These are conservative
guard bands; actual voltage varies with cell condition, temperature, and load.

The Launcher's embedded Slideshow has a one-line status band above the photo. It
shows `BATT`, percentage, and measured voltage. It uses the same filtered state as the menu;
green is 60% or higher, yellow is 25-59%, orange is below 25%, and red means the
low-battery lockout is active.

The Launcher configures PB1 only as an input with its internal weak pull-up, matching
the prior Launcher setup that observed a sustained low level when a cable was
connected on the tested device. It shows this as `CABLE`; it never drives PB1 and
does not call the signal `CHARGING`, because the factory images do not establish that
it represents charge current, completion, or a charger-enable path. The displayed
percentage remains a filtered voltage estimate, not a fuel gauge. Charging remains
under the device's factory hardware control.

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

`build_launcher.bat` defaults to Tetris plus Flappy. `configure_launcher.py`
generates the ordered app labels and module-selection flags, and the build
compiles only the selected modules. The on-device menu scrolls through three
cards at a time when more than three apps are included. Generated
configuration, image data, and build output are ignored by Git.

For a one-off image, check any number of apps in the desktop GUI. If Slideshow
is selected, **Choose up to 3 photos...** can replace its embedded images. The
storage panel projects the combined code, photos, optional screen streamer, and
static RAM. It preserves the 60 KB application region, 4 KB saved-value region,
and a 2 KB runtime stack. The GUI verifies the exact linked size and builds
`build\launcher-custom.bin` separately, so `launcher.bin` is not replaced.

For a direct command-line build with more than two modules, pass quoted app
names through `LAUNCHER_APPS` before running the batch file:

```cmd
set LAUNCHER_APPS="Tetris" "Pac-Man" "Tower Stacker" "Chrome Dino"
build_launcher.bat
```

The flasher is compatible with an ST-Link already attached through `usbipd`; set
`STLINK_BUSID` near the top of `flash_vape.bat` if your adapter uses a different ID.
