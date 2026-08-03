#!/usr/bin/env python3
"""Generate the build-time configuration for a variable Launcher app bundle."""

from __future__ import annotations

import argparse
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent
GENERATED_DIR = PROJECT_DIR / "generated"
HEADER_PATH = GENERATED_DIR / "launcher_config.h"
BATCH_PATH = GENERATED_DIR / "launcher_build_config.bat"

APP_CONFIG = {
    "tetris": ("Tetris", "LAUNCHER_MODULE_TETRIS", "TETRIS"),
    "pac-man": ("Pac-Man", "LAUNCHER_MODULE_PACMAN", "PAC-MAN"),
    "pacman": ("Pac-Man", "LAUNCHER_MODULE_PACMAN", "PAC-MAN"),
    "mario 1-1": ("Mario 1-1", "LAUNCHER_MODULE_MARIO", "MARIO 1-1"),
    "mario": ("Mario 1-1", "LAUNCHER_MODULE_MARIO", "MARIO 1-1"),
    "geometry dash": ("Geometry Dash", "LAUNCHER_MODULE_GEOMETRY_DASH", "GEOMETRY DASH"),
    "chrome dino": ("Chrome Dino", "LAUNCHER_MODULE_CHROME_DINO", "CHROME DINO"),
    "tower stacker": ("Tower Stacker", "LAUNCHER_MODULE_TOWER_STACKER", "TOWER STACKER"),
    "doom": ("Doom", "LAUNCHER_MODULE_DOOM", "DOOM"),
    "flappy": ("Flappy", "LAUNCHER_MODULE_FLAPPY", "FLAPPY BIRD"),
    "slideshow": ("Slideshow", "LAUNCHER_MODULE_SLIDESHOW", "SLIDESHOW"),
}

APP_CHOICES = "Tetris, Pac-Man, Mario 1-1, Geometry Dash, Chrome Dino, Tower Stacker, Doom, Flappy, or Slideshow"
MAX_LAUNCHER_APPS = 9
DEFAULT_LAUNCHER_TITLE = "ConsultingJoe.com"
MAX_LAUNCHER_TITLE_CHARS = 21


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--apps",
        nargs="+",
        required=True,
        metavar="APP",
        help=f"one to {MAX_LAUNCHER_APPS} of: {APP_CHOICES}",
    )
    parser.add_argument(
        "--title",
        default=DEFAULT_LAUNCHER_TITLE,
        help=f"Launcher heading (default: {DEFAULT_LAUNCHER_TITLE})",
    )
    return parser.parse_args()


def normalize_apps(values: list[str]) -> list[tuple[str, str, str]]:
    names = [value.strip().lower() for value in values if value.strip() and value.strip().lower() != "none"]
    names = ["pac-man" if name == "pacman" else name for name in names]
    if not 1 <= len(names) <= MAX_LAUNCHER_APPS:
        raise RuntimeError(f"Launcher requires from one to {MAX_LAUNCHER_APPS} bundled apps.")
    if len(set(names)) != len(names):
        raise RuntimeError("Choose each Launcher app only once.")
    unknown = [name for name in names if name not in APP_CONFIG]
    if unknown:
        raise RuntimeError(
            f"Unknown Launcher app: {unknown[0]}. Choose {APP_CHOICES}."
        )
    return [APP_CONFIG[name] for name in names]


def normalize_title(value: str) -> str:
    title = value.strip()
    if not title:
        raise RuntimeError("Enter a Launcher title.")
    if len(title) > MAX_LAUNCHER_TITLE_CHARS:
        raise RuntimeError(
            f"Launcher title must be {MAX_LAUNCHER_TITLE_CHARS} characters or fewer."
        )
    if any(not (character.isascii() and (character.isalnum() or character in " .-%")) for character in title):
        raise RuntimeError(
            "Launcher title can use letters, numbers, spaces, periods, hyphens, and %."
        )
    return title


def write_configuration(apps: list[tuple[str, str, str]], title: str) -> None:
    GENERATED_DIR.mkdir(exist_ok=True)
    selected = {app[0] for app in apps}
    slot_kinds = ", ".join(app[1] for app in apps)
    slot_labels = ", ".join(f'"{app[2]}"' for app in apps)

    header = (
        "#ifndef GENERATED_LAUNCHER_CONFIG_H\n"
        "#define GENERATED_LAUNCHER_CONFIG_H\n\n"
        "#define LAUNCHER_MODULE_NONE       0u\n"
        "#define LAUNCHER_MODULE_TETRIS     1u\n"
        "#define LAUNCHER_MODULE_FLAPPY     2u\n"
        "#define LAUNCHER_MODULE_SLIDESHOW  3u\n"
        "#define LAUNCHER_MODULE_PACMAN     4u\n"
        "#define LAUNCHER_MODULE_MARIO      5u\n"
        "#define LAUNCHER_MODULE_GEOMETRY_DASH 6u\n"
        "#define LAUNCHER_MODULE_CHROME_DINO 7u\n"
        "#define LAUNCHER_MODULE_DOOM       8u\n"
        "#define LAUNCHER_MODULE_TOWER_STACKER 9u\n\n"
        f'#define LAUNCHER_TITLE "{title}"\n'
        f"#define LAUNCHER_SLOT_COUNT {len(apps)}u\n"
        f"#define LAUNCHER_SLOT_KINDS {{ {slot_kinds} }}\n"
        f"#define LAUNCHER_SLOT_LABELS {{ {slot_labels} }}\n\n"
        f"#define LAUNCHER_HAS_TETRIS {int('Tetris' in selected)}\n"
        f"#define LAUNCHER_HAS_PACMAN {int('Pac-Man' in selected)}\n"
        f"#define LAUNCHER_HAS_FLAPPY {int('Flappy' in selected)}\n"
        f"#define LAUNCHER_HAS_SLIDESHOW {int('Slideshow' in selected)}\n"
        f"#define LAUNCHER_HAS_MARIO {int('Mario 1-1' in selected)}\n"
        f"#define LAUNCHER_HAS_GEOMETRY_DASH {int('Geometry Dash' in selected)}\n"
        f"#define LAUNCHER_HAS_CHROME_DINO {int('Chrome Dino' in selected)}\n"
        f"#define LAUNCHER_HAS_TOWER_STACKER {int('Tower Stacker' in selected)}\n"
        f"#define LAUNCHER_HAS_DOOM {int('Doom' in selected)}\n\n"
        "#endif /* GENERATED_LAUNCHER_CONFIG_H */\n"
    )
    HEADER_PATH.write_text(header, encoding="ascii", newline="\n")

    batch = (
        "@rem Generated by configure_launcher.py\n"
        f"set LAUNCHER_BUILD_TETRIS={int('Tetris' in selected)}\n"
        f"set LAUNCHER_BUILD_PACMAN={int('Pac-Man' in selected)}\n"
        f"set LAUNCHER_BUILD_FLAPPY={int('Flappy' in selected)}\n"
        f"set LAUNCHER_BUILD_SLIDESHOW={int('Slideshow' in selected)}\n"
        f"set LAUNCHER_BUILD_MARIO={int('Mario 1-1' in selected)}\n"
        f"set LAUNCHER_BUILD_GEOMETRY_DASH={int('Geometry Dash' in selected)}\n"
        f"set LAUNCHER_BUILD_CHROME_DINO={int('Chrome Dino' in selected)}\n"
        f"set LAUNCHER_BUILD_TOWER_STACKER={int('Tower Stacker' in selected)}\n"
        f"set LAUNCHER_BUILD_DOOM={int('Doom' in selected)}\n"
    )
    BATCH_PATH.write_text(batch, encoding="ascii", newline="\r\n")

    print(f"Launcher title: {title}")
    print("Launcher bundle: " + " + ".join(app[0] for app in apps))


def main() -> int:
    args = parse_args()
    write_configuration(normalize_apps(args.apps), normalize_title(args.title))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
