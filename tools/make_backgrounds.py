#!/usr/bin/env python3
"""Puts the flat vector guitar pictures (Source/ui/vector, white background) on a wood background -> Source/ui/photos.
Usage: python3 tools/make_backgrounds.py   (then run tools/render_art again)"""
import sys, pathlib
import cv2, numpy as np

root = pathlib.Path(__file__).resolve().parent.parent
src, dst = root / "Source/ui/vector", root / "Source/ui/photos"
rng = np.random.default_rng(7)


def wood(h, w):
    # horizontal planks: stretched noise for the grain, a darker seam between planks, a light vignette
    img = np.zeros((h, w, 3), np.float32)
    plank = 192
    y = 0
    k = 0
    while y < h:
        ph = plank + int(rng.integers(-18, 18))
        base = np.array([62, 104, 158], np.float32) * rng.uniform(0.88, 1.08)   # BGR: warm brown
        noise = rng.standard_normal((ph, w // 24 + 2)).astype(np.float32)
        noise = cv2.resize(noise, (w, ph), interpolation=cv2.INTER_CUBIC)
        fine = cv2.GaussianBlur(rng.standard_normal((ph, w)).astype(np.float32), (0, 0), sigmaX=40, sigmaY=1.1)
        rings = np.sin((np.arange(ph)[:, None] + rng.uniform(0, 50)) * 0.22 + noise * 1.6) * 0.5 + 0.5
        tone = 1.0 + 0.10 * (rings - 0.5) + 0.07 * noise + 0.55 * fine
        img[y:y + ph] = (base[None, None, :] * tone[:, :, None])[: max(0, min(ph, h - y))]
        # seam
        img[y:y + 2] *= 0.55
        # a butt joint now and then
        if k % 2 == 0:
            x = int(rng.integers(w // 4, w - w // 4))
            img[y:y + ph, x:x + 2] *= 0.6
        y += ph
        k += 1
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    v = 1.0 - 0.30 * (((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2) / 2
    return np.clip(img * v[:, :, None], 0, 255)


for f in sorted(src.glob("*.jpg")):
    im = cv2.imread(str(f))
    h, w = im.shape[:2]
    white = (im.min(axis=2) >= 236).astype(np.uint8)
    # background = white connected to the border
    ff = (white * 255).astype(np.uint8).copy()
    pad = cv2.copyMakeBorder(ff, 1, 1, 1, 1, cv2.BORDER_CONSTANT, value=255)
    mask = np.zeros((h + 4, w + 4), np.uint8)
    cv2.floodFill(pad, mask, (0, 0), 128)
    bg = (pad[1:-1, 1:-1] == 128).astype(np.uint8)
    guitar = 1 - bg
    guitar = cv2.morphologyEx(guitar, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))     # specks of the jpeg
    guitar = cv2.erode(guitar, np.ones((3, 3), np.uint8))                              # no white fringe
    a = cv2.GaussianBlur(guitar.astype(np.float32), (0, 0), 0.8)[:, :, None]
    back = wood(h, w)
    # soft shadow of the guitar on the wood
    sh = cv2.GaussianBlur(np.roll(np.roll(guitar.astype(np.float32), 9, axis=0), 6, axis=1), (0, 0), 7)[:, :, None]
    back = back * (1 - 0.45 * sh)
    out = back * (1 - a) + im.astype(np.float32) * a
    cv2.imwrite(str(dst / f.name), np.clip(out, 0, 255).astype(np.uint8), [cv2.IMWRITE_JPEG_QUALITY, 94])
    print(f.name, "guitar pixels %.0f%%" % (100 * guitar.mean()))
