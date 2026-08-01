#!/usr/bin/env python3
"""Run Slideshow's safe direct-flash generator for Launcher firmware."""

from __future__ import annotations

import runpy
import sys
from pathlib import Path


PROJECT_DIR = Path(__file__).resolve().parent
GENERATOR = PROJECT_DIR.parent / "Slideshow" / "gen_direct_flash.py"

if not GENERATOR.is_file():
    raise SystemExit(f"Shared flash generator is missing: {GENERATOR}")

if len(sys.argv) == 1:
    sys.argv.extend([str(PROJECT_DIR / "build" / "launcher.bin"), str(PROJECT_DIR / "direct_flash.tcl")])

sys.argv[0] = str(GENERATOR)
runpy.run_path(str(GENERATOR), run_name="__main__")

