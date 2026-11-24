#!/usr/bin/env python3
"""Check that tidy_svg.py kept what renders: render each raw export and its
tidied copy in headless Chrome and compare the pixels.

Usage: check_tidy.py RAW_DIR TIDY_DIR [--keep RENDER_DIR]
Prints one line per file: pixels differing by more than 8 levels out of
255, and the largest difference. Needs Google Chrome and Pillow. Text needs
the plugin's fonts installed, as in any app opening the SVGs.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"


class TidyCheck:
    def __init__(self, raw_dir, tidy_dir, render_dir):
        self.raw_dir, self.tidy_dir, self.render_dir = raw_dir, tidy_dir, render_dir

    @staticmethod
    def size_of(svg):
        root = ET.parse(svg).getroot()
        return int(float(root.get("width", 800))), int(float(root.get("height", 600)))

    def render(self, svg, png):
        w, h = self.size_of(svg)
        subprocess.run([CHROME, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                        "--force-device-scale-factor=1", f"--window-size={w},{h}",
                        f"--screenshot={png}", svg.resolve().as_uri()],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)

    def compare(self, name):
        from PIL import Image, ImageChops
        pngs = []
        for kind, folder in (("raw", self.raw_dir), ("tidy", self.tidy_dir)):
            png = self.render_dir / f"{Path(name).stem}_{kind}.png"
            self.render(folder / name, png)
            pngs.append(Image.open(png).convert("RGB"))
        diff = ImageChops.difference(*pngs)
        pixels = diff.get_flattened_data() if hasattr(diff, "get_flattened_data") else diff.getdata()
        levels = [max(p) for p in pixels]
        return sum(v > 8 for v in levels), max(levels)

    def run(self):
        worst = 0
        for svg in sorted(self.raw_dir.glob("*.svg")):
            if not (self.tidy_dir / svg.name).exists():
                print(f"{svg.name}: no tidied copy")
                continue
            count, largest = self.compare(svg.name)
            worst = max(worst, largest)
            print(f"{svg.name}: {count} pixels differ by more than 8, largest {largest}")
        return worst


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("raw", type=Path)
    parser.add_argument("tidy", type=Path)
    parser.add_argument("--keep", type=Path, help="keep the renders in this folder")
    args = parser.parse_args()
    if not Path(CHROME).exists():
        print("Google Chrome not found; nothing checked")
        return 0
    render_dir = args.keep or Path(tempfile.mkdtemp(prefix="check_tidy_"))
    render_dir.mkdir(parents=True, exist_ok=True)
    try:
        TidyCheck(args.raw, args.tidy, render_dir).run()
    finally:
        if not args.keep:
            shutil.rmtree(render_dir, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
