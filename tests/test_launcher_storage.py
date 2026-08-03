from __future__ import annotations

import sys
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS_DIR))

from launcher_storage import (  # noqa: E402
    APP_SAFE_BYTES,
    BUNDLE_APPS,
    projected_launcher_bytes,
)
from build_launcher_with_photos import validate_apps  # noqa: E402


class LauncherStorageTests(unittest.TestCase):
    def test_builder_accepts_more_than_two_unique_apps(self) -> None:
        apps = list(BUNDLE_APPS[:5])
        self.assertEqual(validate_apps(apps), apps)

    def test_all_non_slideshow_apps_fit_with_streamer(self) -> None:
        apps = [app for app in BUNDLE_APPS if app != "Slideshow"]
        self.assertLess(projected_launcher_bytes(apps, screen_stream=True), APP_SAFE_BYTES)

    def test_all_apps_fit_with_one_photo_and_streamer(self) -> None:
        self.assertLess(
            projected_launcher_bytes(list(BUNDLE_APPS), screen_stream=True, photo_count=1),
            APP_SAFE_BYTES,
        )

    def test_all_apps_with_two_photos_are_rejected(self) -> None:
        self.assertGreater(
            projected_launcher_bytes(list(BUNDLE_APPS), screen_stream=True, photo_count=2),
            APP_SAFE_BYTES,
        )


if __name__ == "__main__":
    unittest.main()
