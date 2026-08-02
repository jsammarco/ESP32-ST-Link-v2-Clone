#!/usr/bin/env python3
"""Build the Slideshow firmware with one to three locally selected photos.

The photos are copied to a temporary directory only for the build. The output
image is written to RAZ Vape Apps/Slideshow/build/slideshow.bin; generated
image assets remain ignored by Git so personal photos are not committed by
default.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parent.parent
SLIDESHOW_DIR = REPO_ROOT / "RAZ Vape Apps" / "Slideshow"
BUILD_SCRIPT = SLIDESHOW_DIR / "build_slideshow.bat"
MAX_PHOTOS = 3
SUPPORTED_PHOTO_SUFFIXES = {".bmp", ".gif", ".jpeg", ".jpg", ".png", ".webp"}
CUSTOM_APP_NAME = "slideshow-photos"
CUSTOM_IMAGE = SLIDESHOW_DIR / "build" / f"{CUSTOM_APP_NAME}.bin"
GENERATED_HEADER = SLIDESHOW_DIR / "generated" / "photos.h"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--photos", type=Path, nargs="+", required=True, metavar="PHOTO")
    return parser.parse_args()


def normalize_sdk_path(candidate: Path) -> Path | None:
    candidate = candidate.expanduser().resolve()
    if (candidate / "include").is_dir() and (candidate / "src").is_dir():
        return candidate
    if (candidate / "src" / "include").is_dir() and (candidate / "src" / "src").is_dir():
        return candidate / "src"
    return None


def find_vaporware_sdk() -> Path:
    candidates: list[Path] = []
    for variable in ("VAPORWARE", "VAPORWARE_SDK"):
        value = os.environ.get(variable)
        if value:
            candidates.append(Path(value))
    candidates.append(REPO_ROOT.parent / "Vaporware" / "src")

    for candidate in candidates:
        sdk = normalize_sdk_path(candidate)
        if sdk is not None:
            return sdk
    raise RuntimeError(
        "Vaporware SDK not found. Set VAPORWARE_SDK to the SDK root or its src directory "
        "(it must contain include/ and src/)."
    )


def validate_photos(photos: list[Path]) -> list[Path]:
    if not 1 <= len(photos) <= MAX_PHOTOS:
        raise RuntimeError(f"Choose from 1 to {MAX_PHOTOS} photos.")
    resolved: list[Path] = []
    seen: set[Path] = set()
    for photo in photos:
        photo = photo.expanduser().resolve()
        if not photo.is_file():
            raise RuntimeError(f"Photo not found: {photo}")
        if photo.suffix.lower() not in SUPPORTED_PHOTO_SUFFIXES:
            supported = ", ".join(sorted(SUPPORTED_PHOTO_SUFFIXES))
            raise RuntimeError(f"Unsupported photo type for {photo.name}. Choose one of: {supported}")
        if photo in seen:
            raise RuntimeError(f"Photo was selected more than once: {photo.name}")
        seen.add(photo)
        resolved.append(photo)
    return resolved


def main() -> int:
    args = parse_args()
    photos = validate_photos(args.photos)
    if not BUILD_SCRIPT.is_file():
        raise RuntimeError(f"Build script not found: {BUILD_SCRIPT}")
    sdk = find_vaporware_sdk()

    print(f"Building Slideshow with {len(photos)} selected photo(s)...")
    for index, photo in enumerate(photos, start=1):
        print(f"  {index}. {photo.name}")

    original_header = GENERATED_HEADER.read_bytes() if GENERATED_HEADER.is_file() else None
    try:
        with tempfile.TemporaryDirectory(prefix="raz-slideshow-") as temporary:
            staging = Path(temporary)
            for index, photo in enumerate(photos, start=1):
                # The converter sorts by name; a numeric prefix preserves the GUI
                # selection order without retaining the personal originals.
                shutil.copy2(photo, staging / f"{index:02d}_{photo.name}")

            environment = os.environ.copy()
            environment["VAPORWARE"] = str(sdk)
            environment["SLIDESHOW_PHOTOS"] = str(staging)
            environment["SLIDESHOW_APP_NAME"] = CUSTOM_APP_NAME
            result = subprocess.run(["cmd.exe", "/d", "/c", "build_slideshow.bat"], cwd=SLIDESHOW_DIR, env=environment)
            if result.returncode != 0:
                return result.returncode
    finally:
        # Do not leave selected personal photos in generated source assets.
        if original_header is None:
            GENERATED_HEADER.unlink(missing_ok=True)
        else:
            GENERATED_HEADER.write_bytes(original_header)

    image = CUSTOM_IMAGE
    if not image.is_file():
        raise RuntimeError(f"Build completed without producing {image}")
    if image.stat().st_size > 60 * 1024:
        raise RuntimeError(f"Slideshow image is {image.stat().st_size:,} bytes; safe limit is 61,440 bytes.")
    print(f"Built {image} ({image.stat().st_size:,} bytes).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc
