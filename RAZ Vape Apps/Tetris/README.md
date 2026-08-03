# Tetris

Standalone build of the one-button Tetris game used by Launcher.

| Gesture | Action |
|---|---|
| One short press | Move right after the double-tap window |
| Two short presses | Move left |
| Hold 450 ms | Rotate clockwise |

Run `build_tetris.bat` to create `build\tetris.bin`. The build uses the
shared Tetris module under `..\Launcher\src` so standalone and bundled play
remain identical.
