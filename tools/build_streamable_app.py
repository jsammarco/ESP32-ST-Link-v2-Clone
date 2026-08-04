#!/usr/bin/env python3
"""Build a standalone bundled app with the optional SWD screen mirror."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path

from build_slideshow_with_photos import find_vaporware_sdk
from launcher_storage import COIL_OUTPUTS, flashed_image_bytes


REPO_ROOT = Path(os.environ.get("RAZ_REPO_ROOT", Path(__file__).resolve().parent.parent)).resolve()
APP_CONFIG = {
    "Tetris": ("Tetris", "build_tetris.bat", "TETRIS_APP_NAME", "tetris"),
    "Pac-Man": ("Pacman", "build_pacman.bat", "PACMAN_APP_NAME", "pacman"),
    "Mario 1-1": ("Mario", "build_mario.bat", "MARIO_APP_NAME", "mario"),
    "Geometry Dash": ("GeometryDash", "build_geometry_dash.bat", "GEOMETRY_DASH_APP_NAME", "geometry-dash"),
    "Chrome Dino": ("ChromeDino", "build_chrome_dino.bat", "CHROME_DINO_APP_NAME", "chrome-dino"),
    "Tower Stacker": ("TowerStacker", "build_tower_stacker.bat", "TOWER_STACKER_APP_NAME", "tower-stacker"),
    "Doom": ("Doom", "build_doom.bat", "DOOM_APP_NAME", "doom"),
    "Flappy": ("flappy", "build_flappy.bat", "FLAPPY_APP_NAME", "flappy"),
    "Slideshow": ("Slideshow", "build_slideshow.bat", "SLIDESHOW_APP_NAME", "slideshow"),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, choices=APP_CONFIG)
    parser.add_argument("--screen-stream", action="store_true", help="include the SWD screen mirror")
    parser.add_argument("--coil-output", choices=COIL_OUTPUTS, default="pa5")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.app == "Doom" and args.screen_stream:
        raise RuntimeError("The standalone Doom build does not support the SWD screen streamer.")
    directory_name, script_name, app_name_variable, base_name = APP_CONFIG[args.app]
    coil_config = COIL_OUTPUTS[args.coil_output]
    output_name = base_name + coil_config["suffix"] + ("-stream" if args.screen_stream else "")
    app_directory = REPO_ROOT / "RAZ Vape Apps" / directory_name
    build_script = app_directory / script_name
    output_image = app_directory / "build" / f"{output_name}.bin"
    if not build_script.is_file():
        raise RuntimeError(f"Build script not found: {build_script}")

    environment = os.environ.copy()
    environment["VAPORWARE"] = str(find_vaporware_sdk())
    if args.screen_stream:
        environment["SCREEN_STREAMER"] = "1"
    environment["RAZ_COIL_OUTPUT"] = coil_config["build_value"]
    environment[app_name_variable] = output_name

    stream_note = " with screen streaming" if args.screen_stream else ""
    print(f"Building {args.app}{stream_note}; coil output: {coil_config['label']}...")
    result = subprocess.run(
        ["cmd.exe", "/d", "/c", script_name],
        cwd=app_directory,
        env=environment,
    )
    if result.returncode != 0:
        return result.returncode
    if not output_image.is_file():
        raise RuntimeError(f"Build completed without producing {output_image}")
    flashed_bytes = flashed_image_bytes(output_image.stat().st_size)
    if flashed_bytes > 60 * 1024:
        raise RuntimeError(
            f"Image is {flashed_bytes:,} flash bytes; safe limit is 61,440 bytes."
        )
    print(f"Built {output_image} ({flashed_bytes:,} flash bytes).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
