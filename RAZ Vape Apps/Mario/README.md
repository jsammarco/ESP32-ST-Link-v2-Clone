# Mario World 1-1

A standalone, one-button side-scrolling platform game for the RAZ DC25000's
128×160 display. The entire first overworld course is represented in a compact
8-pixel-tile layout, including blocks, pipes, pits, stairs, enemies, coins, the
flagpole, and the castle finish. The app never enables the coil.

| Gesture | Action |
|---|---|
| One short press | Toggle running forward on or off after the double-press window |
| Two short presses within 260 ms | Step backward 16 pixels, then resume the prior run/stop state |
| Hold for 420 ms | Jump once |
| Hold for 10 seconds | Restart the game and restore three lives |

The deliberate 260 ms delay on a single short press lets the app distinguish it
from the first half of a double press. Mario begins stopped with a control card
on screen. A tap dismisses it and starts running. A backstep can reposition
Mario inside the current view, but the camera never scrolls back to a completed
screen.

Run `build_mario.bat` to create `build\mario.bin`. The same app is available as
**Mario 1-1** in the RAZ Manager. If full-resolution screen streaming is checked,
the Manager builds `build\mario-stream.bin` without replacing the normal image.
