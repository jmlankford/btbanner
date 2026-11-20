#!/usr/bin/env python3
"""Regenerate include/font8x16.h — an 8x16 ASCII bitmap font for the sign.

Renders printable ASCII (0x20-0x7E) from a TrueType font with Pillow and packs
each glyph to match the iPixel Color text protocol: one byte per row (top to
bottom); within a byte the LEFTMOST pixel is the LSB (bit 0).

Usage:
    python3 tools/genfont.py [FONT.ttf] [SIZE] [Y_OFFSET]

Defaults: DejaVu Sans Mono, size 14, y-offset 1. Requires Pillow
(`pip install pillow`).
"""
import os
import sys
from PIL import Image, ImageFont, ImageDraw

FONT = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
SIZE = int(sys.argv[2]) if len(sys.argv) > 2 else 14
YOFF = int(sys.argv[3]) if len(sys.argv) > 3 else 1
W, H, THRESH = 8, 16, 128

OUT = os.path.join(os.path.dirname(__file__), "..", "include", "font8x16.h")

font = ImageFont.truetype(FONT, SIZE)
glyphs = []
for cp in range(0x20, 0x7F):
    img = Image.new("L", (W, H), 0)
    ImageDraw.Draw(img).text((0, YOFF), chr(cp), fill=255, font=font)
    rows = []
    for y in range(H):
        b = 0
        for x in range(W):
            if img.getpixel((x, y)) > THRESH:
                b |= (1 << x)  # leftmost pixel -> LSB (bit 0)
        rows.append(b)
    glyphs.append((cp, rows))

with open(OUT, "w") as f:
    f.write(
        "// Auto-generated 8x16 ASCII bitmap font (printable 0x20-0x7E).\n"
        f"// Rendered from {os.path.basename(FONT)} at size {SIZE}. Bit layout matches the\n"
        "// iPixel Color text protocol: one byte per row (top->bottom); within a byte\n"
        "// the LEFTMOST pixel is the LSB (bit 0), x = bit index.\n"
        "// Regenerate with tools/genfont.py.\n"
        "#pragma once\n#include <Arduino.h>\n\n"
        "#define FONT_FIRST_CHAR 0x20\n#define FONT_LAST_CHAR  0x7E\n"
        "#define FONT_WIDTH  8\n#define FONT_HEIGHT 16\n\n"
        "static const uint8_t FONT8X16[FONT_LAST_CHAR - FONT_FIRST_CHAR + 1][FONT_HEIGHT] PROGMEM = {\n"
    )
    for cp, rows in glyphs:
        f.write("  { " + ", ".join(f"0x{b:02X}" for b in rows) + f" }}, // 0x{cp:02X}\n")
    f.write("};\n")

print(f"wrote {OUT} ({len(glyphs)} glyphs)")
