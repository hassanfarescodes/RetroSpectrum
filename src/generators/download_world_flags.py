#!/usr/bin/env python3
"""
Download country flag PNG images for world_map_hover_flags.c.

Source: Flagpedia / FlagCDN bitmap package.
- Downloaded package contains all country flags using ISO 3166-1 alpha-2 filenames.
- Files are extracted to ./flags as lowercase two-letter PNG names.
"""
import io
import os
import sys
import zipfile
from pathlib import Path
from urllib.request import Request, urlopen

ZIP_URL = "https://flagcdn.com/w160.zip"
OUT_DIR = Path("flags")

def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"Downloading {ZIP_URL}")
    req = Request(ZIP_URL, headers={"User-Agent": "RetroSpectrumWorldMap/1.0"})
    with urlopen(req, timeout=60) as r:
        data = r.read()
    print(f"Downloaded {len(data)} bytes")
    z = zipfile.ZipFile(io.BytesIO(data))
    count = 0
    for name in z.namelist():
        if not name.lower().endswith(".png"):
            continue
        base = Path(name).name.lower()
        # package names are usually xx.png
        if len(base) < 6:
            continue
        target = OUT_DIR / base
        target.write_bytes(z.read(name))
        count += 1
    print(f"Extracted {count} PNG flags to {OUT_DIR.resolve()}")
    print("Use this folder next to your executable, or pass its path to WORLD_MAP_draw(..., flags_dir).")

if __name__ == "__main__":
    raise SystemExit(main())
