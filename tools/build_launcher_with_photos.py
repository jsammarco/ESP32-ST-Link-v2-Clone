#!/usr/bin/env python3
"""Build Launcher with one to three photos for its embedded Slideshow.

The selected originals are staged only in a temporary directory. The normal
Launcher photo header is restored after the build, so a custom build does not
replace the bundled Launcher image or leave personal source images behind.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from build_slideshow_with_photos import find_vaporware_sdk, validate_photos


REPO_ROOT = Path(__file__).resolve().parent.parent
LAUNCHER_DIR = REPO_ROOT / "RAZ Vape Apps" / "Launcher"
BUILD_SCRIPT = LAUNCHER_DIR / "build_launcher.bat"
CUSTOM_APP_NAME = "launcher-photos"
CUSTOM_IMAGE = LAUNCHER_DIR / "build" / f"{CUSTOM_APP_NAME}.bin"
GENERATED_HEADER = LAUNCHER_DIR / "generated" / "photos.h"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--photos", type=Path, nargs="+", required=True, metavar="PHOTO")
    return parser.parse_args()


def main() -> int:
    photos = validate_photos(parse_args().photos)
    if not BUILD_SCRIPT.is_file():
        raise RuntimeError(f"Build script not found: {BUILD_SCRIPT}")
    sdk = find_vaporware_sdk()

    print(f"Building Launcher with {len(photos)} embedded Slideshow photo(s)...")
    for index, photo in enumerate(photos, start=1):
        print(f"  {index}. {photo.name}")

    original_header = GENERATED_HEADER.read_bytes() if GENERATED_HEADER.is_file() else None
    try:
        with tempfile.TemporaryDirectory(prefix="raz-launcher-") as temporary:
            staging = Path(temporary)
            for index, photo in enumerate(photos, start=1):
                # The converter sorts inputs; the prefix preserves the picker order.
                shutil.copy2(photo, staging / f"{index:02d}_{photo.name}")

            environment = os.environ.copy()
            environment["VAPORWARE"] = str(sdk)
            environment["LAUNCHER_PHOTOS"] = str(staging)
            environment["LAUNCHER_APP_NAME"] = CUSTOM_APP_NAME
            result = subprocess.run(
                ["cmd.exe", "/d", "/c", "build_launcher.bat"],
                cwd=LAUNCHER_DIR,
                env=environment,
            )
            if result.returncode != 0:
                return result.returncode
    finally:
        if original_header is None:
            GENERATED_HEADER.unlink(missing_ok=True)
        else:
            GENERATED_HEADER.write_bytes(original_header)

    if not CUSTOM_IMAGE.is_file():
        raise RuntimeError(f"Build completed without producing {CUSTOM_IMAGE}")
    if CUSTOM_IMAGE.stat().st_size > 60 * 1024:
        raise RuntimeError(
            f"Launcher image is {CUSTOM_IMAGE.stat().st_size:,} bytes; safe limit is 61,440 bytes."
        )
    print(f"Built {CUSTOM_IMAGE} ({CUSTOM_IMAGE.stat().st_size:,} bytes).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
