#!/usr/bin/env python3
"""Build the heater-disabled N32 RAZ Browser image for RAZ Manager."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from launcher_storage import APP_SAFE_BYTES, flashed_image_bytes


REPO_ROOT = Path(os.environ.get("RAZ_REPO_ROOT", Path(__file__).resolve().parent.parent)).resolve()
BROWSER_PROJECT = REPO_ROOT / "firmware" / "n32g031-poc"
BROWSER_BUILD_SCRIPT = BROWSER_PROJECT / "build.ps1"
BROWSER_IMAGE = BROWSER_PROJECT / "build" / "raz_esp32_poc.bin"


def main() -> int:
    if not BROWSER_BUILD_SCRIPT.is_file():
        raise RuntimeError(f"RAZ Browser build script not found: {BROWSER_BUILD_SCRIPT}")

    print("Building the heater-disabled N32 RAZ Browser firmware...")
    result = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            str(BROWSER_BUILD_SCRIPT),
            "-Target",
            "poc",
        ],
        cwd=BROWSER_PROJECT,
    )
    if result.returncode != 0:
        return result.returncode
    if not BROWSER_IMAGE.is_file():
        raise RuntimeError(f"Build completed without producing {BROWSER_IMAGE}")

    image_bytes = flashed_image_bytes(BROWSER_IMAGE.stat().st_size)
    if image_bytes > APP_SAFE_BYTES:
        raise RuntimeError(
            f"RAZ Browser image is {image_bytes:,} flash bytes; "
            f"the safe app limit is {APP_SAFE_BYTES:,} bytes."
        )
    print(f"Built {BROWSER_IMAGE} ({image_bytes:,} flash bytes).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
