"""Shared Launcher flash-capacity constants and projection helpers."""

from __future__ import annotations

from pathlib import Path

FLASH_TOTAL_BYTES = 64 * 1024
SETTINGS_RESERVED_BYTES = 4 * 1024
APP_SAFE_BYTES = FLASH_TOTAL_BYTES - SETTINGS_RESERVED_BYTES
RAM_TOTAL_BYTES = 8 * 1024
STACK_RESERVED_BYTES = 2 * 1024
APP_RAM_SAFE_BYTES = RAM_TOTAL_BYTES - STACK_RESERVED_BYTES
DEFAULT_LAUNCHER_TITLE = "ConsultingJoe.com"
MAX_LAUNCHER_TITLE_CHARS = 21
STREAMER_ESTIMATE_BYTES = 800
PHOTO_ASSET_BYTES = 10_240 + 32
SLIDESHOW_COMMON_ESTIMATE_BYTES = 5_260

COIL_OUTPUTS = {
    "pa5": {
        "label": "PA5 - regular RAZ (tested)",
        "build_value": "1",
        "suffix": "",
        "note": "Uses the current tested RAZ coil-output pin.",
    },
    "pb8": {
        "label": "PB8 - KRAZ XD0007 V0.6B/G1",
        "build_value": "2",
        "suffix": "-pb8",
        "note": "Uses the published KRAZ coil pin; other KRAZ display/input differences remain experimental.",
    },
    "disabled": {
        "label": "Disabled - do not configure a coil GPIO",
        "build_value": "0",
        "suffix": "-coil-off",
        "note": "The firmware never configures or drives PA5 or PB8 as a coil output.",
    },
}


def coil_output_config(value: str) -> dict[str, str]:
    try:
        return COIL_OUTPUTS[value]
    except KeyError as exc:
        raise ValueError(f"Unknown coil output: {value}") from exc

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
LAUNCHER_COMMON_ESTIMATE_BYTES = 12_000
LAUNCHER_MODULE_ESTIMATE_BYTES = {
    "Tetris": 3_776,
    "Pac-Man": 4_160,
    "Mario 1-1": 6_248,
    "Geometry Dash": 4_428,
    "Chrome Dino": 3_980,
    "Tower Stacker": 4_852,
    "Doom": 5_960,
    "Flappy": 5_184,
    "Slideshow": 1_008,
}

# Static-RAM contributions measured from the module object files. The shared
# strip compositor is linked once when any optimized scrolling game is present.
# Slideshow uses a smaller decoder strip when screen streaming is enabled.
LAUNCHER_COMMON_RAM_BYTES = 52
LAUNCHER_MODULE_RAM_BYTES = {
    "Tetris": 457,
    "Pac-Man": 303,
    "Mario 1-1": 268,
    "Geometry Dash": 39,
    "Chrome Dino": 44,
    "Tower Stacker": 183,
    "Doom": 1_966,
    "Flappy": 332,
    "Slideshow": 4_115,
}
SMOOTH_COMPOSITOR_APPS = {
    "Mario 1-1",
    "Geometry Dash",
    "Chrome Dino",
    "Tower Stacker",
}
SMOOTH_COMPOSITOR_RAM_BYTES = 1_040
SCREEN_STREAMER_RAM_BYTES = 3_116
STREAMED_SLIDESHOW_RAM_BYTES = 1_043


def flashed_image_bytes(image_bytes: int) -> int:
    """Return the word-aligned byte count sent to the target."""
    return image_bytes + ((-image_bytes) % 4)


def projected_launcher_bytes(
    apps: list[str],
    *,
    title: str = DEFAULT_LAUNCHER_TITLE,
    screen_stream: bool = False,
    photo_count: int = 0,
) -> int:
    """Return a close pre-link projection for a selected Launcher bundle."""
    size = LAUNCHER_COMMON_ESTIMATE_BYTES
    size += len(title) - len(DEFAULT_LAUNCHER_TITLE)
    size += sum(LAUNCHER_MODULE_ESTIMATE_BYTES[app] for app in apps)
    if screen_stream:
        size += STREAMER_ESTIMATE_BYTES
    if "Slideshow" in apps:
        size += photo_count * PHOTO_ASSET_BYTES
    return flashed_image_bytes(size)


def projected_launcher_ram_bytes(apps: list[str], *, screen_stream: bool = False) -> int:
    """Return projected static RAM while preserving a separate 2 KB stack."""
    size = LAUNCHER_COMMON_RAM_BYTES
    for app in apps:
        if app == "Slideshow" and screen_stream:
            size += STREAMED_SLIDESHOW_RAM_BYTES
        else:
            size += LAUNCHER_MODULE_RAM_BYTES[app]
    if any(app in SMOOTH_COMPOSITOR_APPS for app in apps):
        size += SMOOTH_COMPOSITOR_RAM_BYTES
    if screen_stream:
        size += SCREEN_STREAMER_RAM_BYTES
    return size + ((-size) % 4)


def validate_launcher_title(value: str) -> str:
    """Return a trimmed title that the Launcher's compact font can display."""
    title = value.strip()
    if not title:
        raise ValueError("Enter a Launcher title.")
    if len(title) > MAX_LAUNCHER_TITLE_CHARS:
        raise ValueError(
            f"Launcher title must be {MAX_LAUNCHER_TITLE_CHARS} characters or fewer."
        )
    unsupported = sorted({character for character in title if not (
        character.isascii()
        and (character.isalnum() or character in " .-%")
    )})
    if unsupported:
        raise ValueError(
            "Launcher title can use letters, numbers, spaces, periods, hyphens, and %."
        )
    return title


def selected_image_bytes(normal: Path, streamed: Path | None, screen_stream: bool) -> tuple[int, bool]:
    """Return image size and whether it came from an exact existing binary."""
    selected = streamed if screen_stream and streamed is not None else normal
    if selected.is_file():
        return flashed_image_bytes(selected.stat().st_size), True
    if normal.is_file():
        return (
            flashed_image_bytes(
                normal.stat().st_size + (STREAMER_ESTIMATE_BYTES if screen_stream else 0)
            ),
            False,
        )
    return 0, False
