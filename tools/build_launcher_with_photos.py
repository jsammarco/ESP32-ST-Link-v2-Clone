#!/usr/bin/env python3
"""Build Launcher with selected apps and optional Slideshow photos.

Selected photo originals are staged only in a temporary directory. The normal
photo header is restored afterward, so a custom build does not replace the
bundled Launcher image or leave personal source images behind.
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
from launcher_storage import APP_SAFE_BYTES, BUNDLE_APPS, FLASH_TOTAL_BYTES, SETTINGS_RESERVED_BYTES


REPO_ROOT = Path(os.environ.get("RAZ_REPO_ROOT", Path(__file__).resolve().parent.parent)).resolve()
LAUNCHER_DIR = REPO_ROOT / "RAZ Vape Apps" / "Launcher"
BUILD_SCRIPT = LAUNCHER_DIR / "build_launcher.bat"
CUSTOM_APP_NAME = "launcher-custom"
GENERATED_HEADER = LAUNCHER_DIR / "generated" / "photos.h"
def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apps", nargs="+", required=True, choices=BUNDLE_APPS, metavar="APP")
    parser.add_argument("--photos", type=Path, nargs="*", metavar="PHOTO")
    parser.add_argument("--screen-stream", action="store_true", help="include the SWD screen mirror")
    return parser.parse_args()


def validate_apps(apps: list[str]) -> list[str]:
    if not 1 <= len(apps) <= len(BUNDLE_APPS):
        raise RuntimeError(f"Choose from one to {len(BUNDLE_APPS)} apps for Launcher.")
    if len(set(apps)) != len(apps):
        raise RuntimeError("Choose each Launcher app only once.")
    return apps


def main() -> int:
    args = parse_args()
    apps = validate_apps(args.apps)
    photos = validate_photos(args.photos) if args.photos else []
    if photos and "Slideshow" not in apps:
        raise RuntimeError("Embedded photos require Slideshow in one of the Launcher slots.")
    if not BUILD_SCRIPT.is_file():
        raise RuntimeError(f"Build script not found: {BUILD_SCRIPT}")
    sdk = find_vaporware_sdk()
    output_name = CUSTOM_APP_NAME + ("-stream" if args.screen_stream else "")
    custom_image = LAUNCHER_DIR / "build" / f"{output_name}.bin"

    print("Building Launcher bundle: " + " + ".join(apps))
    if photos:
        print(f"Embedding {len(photos)} Slideshow photo(s):")
        for index, photo in enumerate(photos, start=1):
            print(f"  {index}. {photo.name}")

    original_header = GENERATED_HEADER.read_bytes() if GENERATED_HEADER.is_file() else None
    try:
        with tempfile.TemporaryDirectory(prefix="raz-launcher-") as temporary:
            environment = os.environ.copy()
            environment["VAPORWARE"] = str(sdk)
            environment["LAUNCHER_APP_NAME"] = output_name
            environment["LAUNCHER_APPS"] = " ".join(f'"{app}"' for app in apps)
            if args.screen_stream:
                environment["SCREEN_STREAMER"] = "1"
            if photos:
                staging = Path(temporary)
                for index, photo in enumerate(photos, start=1):
                    # The converter sorts inputs; the prefix preserves picker order.
                    shutil.copy2(photo, staging / f"{index:02d}_{photo.name}")
                environment["LAUNCHER_PHOTOS"] = str(staging)
            result = subprocess.run(
                ["cmd.exe", "/d", "/c", "build_launcher.bat"],
                cwd=LAUNCHER_DIR,
                env=environment,
            )
            if result.returncode != 0:
                return result.returncode
    finally:
        if photos:
            if original_header is None:
                GENERATED_HEADER.unlink(missing_ok=True)
            else:
                GENERATED_HEADER.write_bytes(original_header)

    if not custom_image.is_file():
        raise RuntimeError(f"Build completed without producing {custom_image}")
    image_bytes = custom_image.stat().st_size
    print(
        f"FLASH_USAGE {image_bytes} {APP_SAFE_BYTES} {FLASH_TOTAL_BYTES} "
        f"(settings reserve {SETTINGS_RESERVED_BYTES} bytes)"
    )
    if image_bytes > APP_SAFE_BYTES:
        raise RuntimeError(
            f"Launcher image is {image_bytes:,} bytes; safe app limit is {APP_SAFE_BYTES:,} bytes."
        )
    print(f"Built {custom_image} ({image_bytes:,} bytes).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
