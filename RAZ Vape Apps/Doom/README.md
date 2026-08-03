# Doom

A small first-person shooter built for the vape's 128x160 display. It uses an original low-resolution renderer, maze, and pixel art; no original Doom game data is included.

## Controls

- Draw on the mouthpiece: fire the plasma rifle and run the coil through the verified PA3 pressure signal.
- One short button tap: turn right after a 0.3-second gesture window, using a finer turn step for aiming.
- Two short button taps within 0.3 seconds: turn left without first jumping the view to the right.
- Hold the button for about 0.15 seconds: walk forward; keep holding for faster, smoother continuous movement.
- Ten short button taps within about 2.2 seconds: turn the game screen off. One button press wakes it without moving or turning.
- After 30 seconds without a button press, the device enters low-power sleep. Press the button to wake it.

Enemies use scaled multi-colour pixel art with horns, claws, glowing eyes, hit
feedback, and per-column wall occlusion.

## Build and flash

Run `build_doom.bat`, then `flash_vape.bat` from this folder. The flasher uses the same USBIPD/WSL handling as Launcher and writes only the MCU firmware.

## Coil safeguards

The coil uses normal-mode 50% duty while the PA3 draw is active. It stops immediately when the draw ends, has an 1.8-second hard cutoff, and requires a release before another draw can start. The game never enables boost mode.
