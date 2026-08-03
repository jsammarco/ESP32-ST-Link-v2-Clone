# Geometry Dash

A standalone, one-button rhythm-platform game for the RAZ DC25000's 128×160
display. The cube automatically runs through a complete neon course containing
spikes, pits, raised platforms, automatic jump pads, optional midair jump orbs,
speed portals, and a finish gate. The app never enables the coil.

| Input | Action |
|---|---|
| Press | Start the attempt and jump |
| Press while running | Jump from the ground or activate a nearby yellow jump orb |
| Hold | Automatically jump again whenever the cube lands |
| Press after a crash | Start the next attempt |
| Hold for 10 seconds | Reset to attempt one |

The HUD shows the current attempt and completion percentage. The magenta portal
starts the fast section; the cyan portal returns to normal speed.

Run `build_geometry_dash.bat` to create `build\geometry-dash.bin`. The same app
is available as **Geometry Dash** in the RAZ Manager. With full-resolution screen
streaming checked, the Manager builds `build\geometry-dash-stream.bin` without
replacing the normal image.
