#!/usr/bin/env python3
"""Build a standalone bundled app with the optional SWD screen mirror."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path

from build_slideshow_with_photos import find_vaporware_sdk


REPO_ROOT = Path(os.environ.get("RAZ_REPO_ROOT", Path(__file__).resolve().parent.parent)).resolve()
APP_CONFIG = {
    "Tetris": ("Tetris", "build_tetris.bat", "TETRIS_APP_NAME", "tetris-stream"),
    "Pac-Man": ("Pacman", "build_pacman.bat", "PACMAN_APP_NAME", "pacman-stream"),
    "Mario 1-1": ("Mario", "build_mario.bat", "MARIO_APP_NAME", "mario-stream"),
    "Geometry Dash": ("GeometryDash", "build_geometry_dash.bat", "GEOMETRY_DASH_APP_NAME", "geometry-dash-stream"),
    "Chrome Dino": ("ChromeDino", "build_chrome_dino.bat", "CHROME_DINO_APP_NAME", "chrome-dino-stream"),
    "Tower Stacker": ("TowerStacker", "build_tower_stacker.bat", "TOWER_STACKER_APP_NAME", "tower-stacker-stream"),
    "Flappy": ("flappy", "build_flappy.bat", "FLAPPY_APP_NAME", "flappy-stream"),
    "Slideshow": ("Slideshow", "build_slideshow.bat", "SLIDESHOW_APP_NAME", "slideshow-stream"),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, choices=APP_CONFIG)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    directory_name, script_name, app_name_variable, output_name = APP_CONFIG[args.app]
    app_directory = REPO_ROOT / "RAZ Vape Apps" / directory_name
    build_script = app_directory / script_name
    output_image = app_directory / "build" / f"{output_name}.bin"
    if not build_script.is_file():
        raise RuntimeError(f"Build script not found: {build_script}")

    environment = os.environ.copy()
    environment["VAPORWARE"] = str(find_vaporware_sdk())
    environment["SCREEN_STREAMER"] = "1"
    environment[app_name_variable] = output_name

    print(f"Building stream-enabled {args.app}...")
    result = subprocess.run(
        ["cmd.exe", "/d", "/c", script_name],
        cwd=app_directory,
        env=environment,
    )
    if result.returncode != 0:
        return result.returncode
    if not output_image.is_file():
        raise RuntimeError(f"Build completed without producing {output_image}")
    if output_image.stat().st_size > 60 * 1024:
        raise RuntimeError(
            f"Image is {output_image.stat().st_size:,} bytes; safe limit is 61,440 bytes."
        )
    print(f"Built {output_image} ({output_image.stat().st_size:,} bytes).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
