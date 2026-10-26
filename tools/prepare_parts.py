#!/usr/bin/env python3
"""Cuts the knob / selector parts out of the green-screen sheets (made with Gemini) into transparent PNGs.

  python3 tools/prepare_parts.py <folder with the sheets>

Writes Source/ui/parts/<name>.png (trimmed, shadow of the sheet removed) and Source/ui/parts/anchors.txt (name x y: the centre
of the knob / the mounting nut / the plate in the PNG, in pixels). tools/render_art.cpp composes the pictures from them.
Needs: pip install opencv-python-headless numpy
"""
import os
import sys

import cv2
import numpy as np

# sheet file -> names of the parts from left to right, and the anchor of every part as an offset in the sheet (None = centre of the part)
SHEETS = {
    "gibson style.jpg": (["gib_vol", "gib_tone", "gib_tog_l", "gib_tog_c", "gib_tog_r"],
                         [None, None, (691, 385), (927, 383), (1163, 383)]),
    "rickenbacker style.jpeg": (["ric_vol", "ric_tone", "ric_tog"], [None, None, (995, 372)]),
    "se 24 style.jpeg": (["amb_vol", "amb_tone", "prs_l", "prs_c", "prs_r"],
                         [None, None, (650, 385), (903, 385), (1150, 385)]),
    "str style.jpeg": (["crm_vol", "crm_tone", "str_1", "str_2", "str_3", "str_4", "str_5"],
                       [None, None, (532, 397), (707, 397), (882, 397), (1055, 397), (1232, 397)]),
    "tele style.jpeg": (["chr_vol", "chr_tone", "tele_l", "tele_c", "tele_r"],
                        [None, None, (588, 388), (867, 388), (1148, 388)]),
}


def cut(img):
    """-> BGRA image of the whole sheet (alpha = part, green background and its shadow removed)."""
    f = img.astype(np.float32)
    b, g, r = f[..., 0], f[..., 1], f[..., 2]
    green = g - np.maximum(r, b)
    border = np.concatenate([green[:20].ravel(), green[-20:].ravel(), green[:, :20].ravel(), green[:, -20:].ravel()])
    bg = float(np.median(border))
    alpha = np.clip((0.70 * bg - green) / (0.30 * bg), 0.0, 1.0)
    alpha = cv2.GaussianBlur(alpha, (0, 0), 0.7)
    out = img.copy()
    out[..., 1] = np.minimum(out[..., 1], np.maximum(out[..., 2], out[..., 0]))        # despill
    return np.dstack([out, (alpha * 255).astype(np.uint8)])


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    src = sys.argv[1]
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "ui", "parts")
    os.makedirs(out_dir, exist_ok=True)
    anchors = []
    for sheet, (names, anch) in SHEETS.items():
        img = cv2.imread(os.path.join(src, sheet))
        if img is None:
            print("missing", sheet)
            return 1
        rgba = cut(img)
        solid = (rgba[..., 3] > 235).astype(np.uint8)
        merged = cv2.dilate(solid, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9)))
        n, lab, stats, _ = cv2.connectedComponentsWithStats(merged)
        comps = [i for i in range(1, n) if stats[i, cv2.CC_STAT_AREA] > 3000]
        comps.sort(key=lambda i: stats[i, cv2.CC_STAT_LEFT])
        if len(comps) != len(names):
            print(sheet, "found", len(comps), "parts, expected", len(names))
            return 1
        for name, i, a in zip(names, comps, anch):
            x, y, w, h = (stats[i, k] for k in (cv2.CC_STAT_LEFT, cv2.CC_STAT_TOP, cv2.CC_STAT_WIDTH, cv2.CC_STAT_HEIGHT))
            x0, y0, x1, y1 = max(0, x - 4), max(0, y - 4), min(img.shape[1], x + w + 4), min(img.shape[0], y + h + 4)
            part = rgba[y0:y1, x0:x1].copy()
            part[..., 3] = np.where(lab[y0:y1, x0:x1] == i, part[..., 3], 0)
            cx, cy = (a if a else (x + w / 2.0, y + h / 2.0))
            anchors.append("%s %.1f %.1f %d %d" % (name, cx - x0, cy - y0, part.shape[1], part.shape[0]))
            cv2.imwrite(os.path.join(out_dir, name + ".png"), part)
            print(name, part.shape[1], part.shape[0])
    with open(os.path.join(out_dir, "anchors.txt"), "w") as f:
        f.write("\n".join(anchors) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
