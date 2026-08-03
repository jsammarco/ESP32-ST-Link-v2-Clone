# Chrome Dino

A standalone recreation of the Google Chrome offline dinosaur runner for the
RAZ DC25000's 128x160 display. It uses the familiar monochrome pixel art,
animated T-Rex, scrolling ground, clouds, cacti, pterodactyls, five-digit score,
session high score, milestone flash, speed ramp, and alternating day/night
palette. The app is display-only and never enables the coil.

| Input | Action |
|---|---|
| Press on the title screen | Start and jump |
| Press while running | Jump, when the T-Rex is on the ground |
| Press after a crash | Start a new run and jump |
| Hold for 10 seconds | Return to the title screen |

Run `build_chrome_dino.bat` to create `build\chrome-dino.bin`. The game is also
available as **Chrome Dino** in the RAZ Manager. With full-resolution screen
streaming checked, the Manager builds `build\chrome-dino-stream.bin` without
replacing the normal image.
