# Tower Stacker

A polished one-button tower-building game for the RAZ DC25000 display. Floors
move automatically from side to side. Press at the right moment to place the
current floor; unsupported overhang breaks away and makes every later floor
narrower. A complete miss ends the run.

| Screen | Button action |
|---|---|
| Title | Press to start |
| Playing | Press to drop the moving floor and immediately launch the next one |
| Game over | Press to retry |
| Any screen | Hold 10 seconds to reset the session best |

Near-perfect placements snap into exact alignment and increase the `PERFECT`
combo. The tower view climbs automatically, movement gets faster as the score
rises, and falling overhangs, particles, skyline themes, and animated feedback
make each placement readable on the 128×160 screen.

The app is display-only and forces the coil output off at startup and every
frame.

Run `build_tower_stacker.bat` to create `build\tower-stacker.bin`. Set
`SCREEN_STREAMER=1` before running the script to create a build compatible with
the native full-resolution SWD screen viewer.
