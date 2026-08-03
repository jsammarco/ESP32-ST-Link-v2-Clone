"""Shared Launcher flash-capacity constants and projection helpers."""

from __future__ import annotations

from pathlib import Path


FLASH_TOTAL_BYTES = 64 * 1024
SETTINGS_RESERVED_BYTES = 4 * 1024
APP_SAFE_BYTES = FLASH_TOTAL_BYTES - SETTINGS_RESERVED_BYTES
STREAMER_ESTIMATE_BYTES = 400
PHOTO_ASSET_BYTES = 10_240 + 32
SLIDESHOW_COMMON_ESTIMATE_BYTES = 5_260

BUNDLE_APPS = (
    "Tetris",
    "Pac-Man",
    "Mario 1-1",
    "Geometry Dash",
    "Chrome Dino",
    "Tower Stacker",
    "Doom",
    "Flappy",
    "Slideshow",
)

# These are linked-code contributions measured from one-app Launcher builds.
# The common runtime/menu cost is counted once. Exact size is still checked
# after linking because compiler or asset changes can move the final result.
# Includes a conservative 2.25 KB margin observed when every module is linked.
# This keeps the live bar slightly above the final binary rather than promising
# space that link-time glue later consumes.
LAUNCHER_COMMON_ESTIMATE_BYTES = 11_250
LAUNCHER_MODULE_ESTIMATE_BYTES = {
    "Tetris": 3_776,
    "Pac-Man": 4_160,
    "Mario 1-1": 6_248,
    "Geometry Dash": 4_428,
    "Chrome Dino": 3_980,
    "Tower Stacker": 4_852,
    "Doom": 5_692,
    "Flappy": 5_184,
    "Slideshow": 1_008,
}


def projected_launcher_bytes(
    apps: list[str],
    *,
    screen_stream: bool = False,
    photo_count: int = 0,
) -> int:
    """Return a close pre-link projection for a selected Launcher bundle."""
    size = LAUNCHER_COMMON_ESTIMATE_BYTES
    size += sum(LAUNCHER_MODULE_ESTIMATE_BYTES[app] for app in apps)
    if screen_stream:
        size += STREAMER_ESTIMATE_BYTES
    if "Slideshow" in apps:
        size += photo_count * PHOTO_ASSET_BYTES
    return size


def selected_image_bytes(normal: Path, streamed: Path | None, screen_stream: bool) -> tuple[int, bool]:
    """Return image size and whether it came from an exact existing binary."""
    selected = streamed if screen_stream and streamed is not None else normal
    if selected.is_file():
        return selected.stat().st_size, True
    if normal.is_file():
        return normal.stat().st_size + (STREAMER_ESTIMATE_BYTES if screen_stream else 0), False
    return 0, False
