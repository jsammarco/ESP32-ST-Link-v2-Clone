# Slideshow

`Slideshow` turns the vape display into a photo slideshow and adds two deliberately
limited coil-output modes. Images are read from the shared
[`examples/photos`](../photos) folder when the project is built; the generated image
header is intentionally ignored so personal photos are never committed with the app.

> **Safety:** this example can energise the physical coil. It has only software
> time limits: it does not measure coil temperature, resistance, liquid level, or
> airflow. Confirm the coil-gate pin for the specific board before use, keep the
> maximum times conservative, never leave it unattended, and disconnect power before
> changing hardware. The default limits are safeguards, not a replacement for a
> properly characterised vaping-control system.

## Controls

| Gesture | Action |
| --- | --- |
| Tap | Show the next photo |
| Double-tap | Toggle **Normal** / **Boost** output mode |
| Hold for 650 ms | Fire the selected mode until release or its hard cutoff |

The screen also advances automatically every eight seconds. A small marker in the
top-left is green for Normal, purple for Boost, and red while the coil is active.

Normal is a 50% software duty cycle with a 1.8-second cutoff. Boost is continuous
output with a 0.9-second cutoff. Those values are defined at the top of
[`src/main.c`](src/main.c) and should only be changed after validating the actual
hardware.

## Build and flash

From a Command Prompt in this folder:

```cmd
build_slideshow.bat
python gen_direct_flash.py
flash_vape.bat
```

`build_slideshow.bat` first runs `convert_images.py`, then compiles
`build\slideshow.bin`. The converter needs Pillow once on the host:

```cmd
py -m pip install pillow
```

The image assets are encoded as 128x160, 16-colour indexed frames (10,240 bytes per
photo). To leave enough room for the firmware in the 64 KB flash, the default build
accepts at most three photos. Edit or replace files in `examples/photos`, then build
again.

### Convert images manually

```cmd
python convert_images.py
python convert_images.py --input ..\photos --output generated\photos.h --max-images 3
```

Supported input formats are the formats Pillow can open (including PNG, JPEG, BMP,
and WebP). Images are centre-cropped to the display's 4:5 aspect ratio before being
resized, so no black bars are introduced.

### Programmer configuration

`flash_vape.bat` uses WSL/OpenOCD and has a `STLINK_BUSID` setting near its top.
Replace the default `3-1` with the bus ID reported by `usbipd list` on the host.

