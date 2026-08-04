# Geometry Dash

A standalone, one-button gravity-platform game for the RAZ DC25000's 128×160
display. The cube automatically runs through a neon course containing floor
spikes, hanging ceiling spikes, suspended diamond hazards, pits, solid
platforms, speed portals, and a finish gate. The app never enables the coil.

| Input | Action |
|---|---|
| Press | Start the attempt |
| Press while running | Reverse gravity between floor and ceiling |
| Press after a crash | Start the next attempt |
| Hold for 10 seconds | Reset to attempt one |

The HUD shows the current attempt and completion percentage. A gravity-up cube
turns yellow. The magenta portal marks the fast section; the cyan portal marks
the return to normal speed.

Run `build_geometry_dash.bat` to create `build\geometry-dash.bin`. The same app
is available as **Geometry Dash** in the RAZ Manager. With full-resolution screen
streaming checked, the Manager builds `build\geometry-dash-stream.bin` without
replacing the normal image.
