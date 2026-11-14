#!/usr/bin/env python3
"""Regenerates src/assets/logo_png.hpp from assets/logo.svg.
Needs rsvg-convert (librsvg) on PATH.  Usage: python tools/embed_logo.py"""
import pathlib, subprocess, sys

root = pathlib.Path(__file__).resolve().parent.parent
svg, png = root / "assets/logo.svg", root / "assets/logo.png"
subprocess.run(["rsvg-convert", "-w", "128", "-h", "128", str(svg), "-o", str(png)], check=True)
data = png.read_bytes()
rows = ["    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
out = root / "src/assets/logo_png.hpp"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(
    "#pragma once\n// Generated from assets/logo.svg by tools/embed_logo.py. Do not edit by hand.\n"
    "#include <cstddef>\n\ninline constexpr unsigned char kLogoPng[] = {\n" + "\n".join(rows) +
    "\n};\ninline constexpr size_t kLogoPngSize = sizeof(kLogoPng);\n")
print("wrote", out, len(data), "bytes")
