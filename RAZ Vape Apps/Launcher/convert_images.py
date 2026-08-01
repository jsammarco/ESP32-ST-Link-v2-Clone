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

if len(sys.argv) == 1:
    sys.argv.extend(
        [
            "--input", str(PROJECT_DIR.parent / "photos"),
            "--output", str(PROJECT_DIR / "generated" / "photos.h"),
            "--max-images", "3",
        ]
    )

sys.argv[0] = str(CONVERTER)
runpy.run_path(str(CONVERTER), run_name="__main__")

