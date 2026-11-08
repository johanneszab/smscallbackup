#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Johannes Meyer zum Alten Borgloh
# SPDX-License-Identifier: MIT
"""Writes icons/smscallbackup.svg and renders the PNG launcher icons from it.

Needs Python with Pillow and librsvg's GObject bindings (python3-gi, gir1.2-rsvg-2.0).
Run from the repository root: python3 tools/make-icon.py
"""
import math
import os

SIZES = (86, 108, 128, 172, 256)


def arrow(cx, cy, r, start, end, width, head):
    """A clockwise circular arrow from angle start to end (degrees, SVG orientation)."""
    def point(angle, radius=r):
        a = math.radians(angle)
        return cx + radius * math.cos(a), cy + radius * math.sin(a)
    x0, y0 = point(start)
    # stop the stroke a little early so the round cap hides under the arrow head
    x1, y1 = point(end - 8)
    large = 1 if (end - start) % 360 > 180 else 0
    stroke = (f'<path d="M {x0:.2f} {y0:.2f} A {r} {r} 0 {large} 1 {x1:.2f} {y1:.2f}" '
              f'fill="none" stroke="#00897b" stroke-width="{width}" stroke-linecap="round"/>')
    # head: tip ahead of the end point along the direction of travel
    a = math.radians(end)
    tx, ty = -math.sin(a), math.cos(a)          # tangent, clockwise
    ex, ey = point(end)
    tip = (ex + tx * head, ey + ty * head)
    outer = point(end, r + head * 0.85)
    inner = point(end, r - head * 0.85)
    head_path = (f'<path d="M {tip[0]:.2f} {tip[1]:.2f} L {outer[0]:.2f} {outer[1]:.2f} '
                 f'L {inner[0]:.2f} {inner[1]:.2f} Z" fill="#00897b"/>')
    return stroke + "\n  " + head_path


def svg():
    return f'''<?xml version="1.0" encoding="UTF-8"?>
<!-- SMS & Call Backup launcher icon. SPDX-License-Identifier: MIT -->
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <defs>
    <linearGradient id="background" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0" stop-color="#1e88e5"/>
      <stop offset="1" stop-color="#00897b"/>
    </linearGradient>
  </defs>
  <rect x="8" y="8" width="240" height="240" rx="64" fill="url(#background)"/>
  <path d="M 70 46 H 160 A 30 30 0 0 1 190 76 V 126 A 30 30 0 0 1 160 156 H 108 L 74 186 V 156 H 70 A 30 30 0 0 1 40 126 V 76 A 30 30 0 0 1 70 46 Z" fill="#ffffff"/>
  <rect x="64" y="78" width="100" height="14" rx="7" fill="#90caf9"/>
  <rect x="64" y="110" width="70" height="14" rx="7" fill="#90caf9"/>
  <circle cx="182" cy="182" r="54" fill="#ffffff"/>
  <circle cx="182" cy="182" r="54" fill="none" stroke="#00897b" stroke-opacity="0.15" stroke-width="2"/>
  {arrow(182, 182, 30, -70, 190, 11, 15)}
</svg>
'''


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(root, "icons", "smscallbackup.svg")
    with open(path, "w") as f:
        f.write(svg())

    import io
    import gi
    gi.require_version("Rsvg", "2.0")
    from gi.repository import Rsvg
    from PIL import Image
    # Render big once, then scale down: sharper small icons than rendering each size.
    big = svg().replace('width="256" height="256"', 'width="1024" height="1024"')
    pixbuf = Rsvg.Handle.new_from_data(big.encode("utf-8")).get_pixbuf()
    ok, data = pixbuf.save_to_bufferv("png", [], [])
    master = Image.open(io.BytesIO(data)).convert("RGBA")
    for size in SIZES:
        out_dir = os.path.join(root, "icons", f"{size}x{size}")
        os.makedirs(out_dir, exist_ok=True)
        master.resize((size, size), Image.LANCZOS).save(os.path.join(out_dir, "smscallbackup.png"))
    print("wrote", path, "and", ", ".join(f"{s}x{s}" for s in SIZES))


if __name__ == "__main__":
    main()
