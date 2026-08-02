#!/usr/bin/env python3
"""Run Slideshow's photo converter with Launcher-friendly default paths."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent
CONVERTER = PROJECT_DIR.parent / "Slideshow" / "convert_images.py"

if not CONVERTER.is_file():
    raise SystemExit(f"Shared photo converter is missing: {CONVERTER}")

arguments = sys.argv[1:]


def has_option(name: str) -> bool:
    return any(argument == name or argument.startswith(f"{name}=") for argument in arguments)


if not has_option("--input"):
    arguments.extend(["--input", str(PROJECT_DIR.parent / "photos")])
if not has_option("--output"):
    arguments.extend(["--output", str(PROJECT_DIR / "generated" / "photos.h")])
if not has_option("--max-images"):
    arguments.extend(["--max-images", "3"])

sys.argv = [str(CONVERTER), *arguments]
runpy.run_path(str(CONVERTER), run_name="__main__")
