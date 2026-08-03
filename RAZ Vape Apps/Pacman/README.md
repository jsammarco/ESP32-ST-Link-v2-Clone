# Pac-Man

Standalone build of the one-button maze chase used by Launcher. It uses the
canonical 28x31 arcade maze with 240 dots and four power pellets. Pac-Man moves
automatically and remembers a requested turn until it becomes legal.

| Gesture | Action |
|---|---|
| One short press | Queue a right turn immediately on release |
| Two short presses | Queue a left turn |
| Hold 450 ms | Reverse direction |

Pac-Man advances one tile every 480 ms. A single unambiguous corner is followed
automatically, and a dead end automatically reverses, so the one-button input
cannot leave him trapped. The second press of a double-tap is registered
immediately to make left turns easier near intersections.

Eat all 244 pellets to advance to a faster level. Power pellets frighten the
three ghosts for eight seconds. The game never requests coil output.

Run `build_pacman.bat` to create `build\pacman.bin`. The build uses the shared
Pac-Man module under `..\Launcher\src` so standalone and bundled play remain
identical.
