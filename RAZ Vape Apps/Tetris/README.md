# Tetris

Standalone build of the one-button Tetris game used by Launcher.

| Gesture | Action |
|---|---|
| One short press | Move right after the double-tap window |
| Two short presses | Move left |
| Hold 450 ms | Rotate clockwise |
| Draw from pressure sensor | Hard-drop and lock the active piece |

Clearing a row opens a 1.5-second `VAPE NOW` reward window. The coil requires
an active pressure-sensor draw during that window and switches off immediately
on release or expiry. A draw outside the reward window only hard-drops the piece.

Run `build_tetris.bat` to create `build\tetris.bin`. The build uses the
shared Tetris module under `..\Launcher\src` so standalone and bundled play
remain identical.
