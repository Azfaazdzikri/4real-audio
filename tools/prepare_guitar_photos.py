#!/usr/bin/env python3
"""Prepares the guitar photos that Source/ui shows in the plugin.

  python3 tools/prepare_guitar_photos.py <folder with the original photos> [--debug]

Reads the 8 original photos and writes the files that are embedded in the plugin to Source/ui/photos/<CODE>.jpg:
  * brand names / logos are removed (FLC: GRETSCH + Bigsby text on the tailpiece, GRETSCH on the pickguard, the emblem on the
    four gold knob caps). The Rickenbacker "R" tailpiece is kept on purpose.
  * the lever tip of the pickup selector of every guitar is removed; the plugin draws its own lever there so that it can move.
Nothing is blurred: removed areas are refilled with real texture copied from the surroundings (an exemplar-based fill, see refill()), the knob caps get a plain metal face with the grain of the photo, and every pixel outside
the removed areas is exactly the original pixel. With --debug it also writes <CODE>_mask.png (removed areas in red) next to the output.

Needs: pip install opencv-python-headless numpy pillow
"""
import os
import sys

import cv2
import numpy as np
from PIL import Image

FILES = {  # code -> file name of the original
    "LP": "LP.jpeg", "335": "335.jpeg", "SG": "SG.jpeg", "PRS": "SE24.jpeg",
    "RIC": "R 330.jpeg", "STR": "Strato.jpeg", "TCR": "Tele.jpeg", "FLC": "G White Falcon.jpeg",
}

# Areas to remove. Pixel coordinates in the 1376 x 768 originals.
#   ("poly", [(x, y), ...])           refilled with texture from around it
#   ("cap",  cx, cy, r_cap, r_mask)   a round metal knob cap: the logo inside r_mask is replaced by a plain face
EDITS = {
    "FLC": [
        ("poly", [(77, 355), (94, 352), (111, 379), (126, 408), (127, 419), (108, 421), (97, 415), (87, 393), (77, 364)]),  # GRETSCH plate on the Bigsby
        ("poly", [(52, 356), (68, 356), (70, 372), (68, 410), (52, 412)]),                 # "Bigsby" script
        ("poly", [(370, 520), (386, 523), (398, 529), (415, 529), (428, 523), (428, 532), (372, 539)]),  # GRETSCH on the pickguard
        ("cap", 262, 494, 15, 13), ("cap", 205, 551, 15, 13),                              # emblems on the gold knob caps
        ("cap", 265, 621, 15, 13), ("cap", 598, 529, 14, 12),
        ("poly", [(633, 240), (669, 240), (669, 277), (633, 277)]),                        # toggle cap (lever tip)
    ],
    # --- lever tips of the pickup selectors (the plugin draws them) ---
    "LP": [("poly", [(549, 256), (566, 256), (566, 266), (549, 266)])],
    "335": [("poly", [(261, 507), (276, 507), (279, 519), (276, 527), (263, 527), (259, 518)])],
    "SG": [("poly", [(243, 533), (262, 533), (264, 545), (258, 553), (245, 553), (241, 543)])],
    "PRS": [
        ("poly", [(172, 536), (178, 524), (190, 520), (202, 524), (203, 536), (198, 546), (184, 550), (174, 546)]),
        ("poly", [(207, 527), (214, 520), (223, 518), (224, 524), (215, 530)]),            # white blade in the slot
    ],
    "RIC": [("poly", [(299, 538), (309, 534), (315, 540), (315, 551), (305, 553), (299, 547)])],
    "STR": [("poly", [(319, 533), (325, 527), (334, 527), (340, 533), (340, 543), (332, 546), (322, 543)])],
    "TCR": [("shift", [(252, 499), (290, 499), (290, 530), (252, 530)], -84)],   # plain plate copied from between the knobs
}


def poly_mask(shape, pts):
    m = np.zeros(shape[:2], np.uint8)
    cv2.fillPoly(m, [np.array(pts, np.int32)], 255)
    return m


def refill(img, mask, patch=9, search=34, margin=48):
    """Exemplar-based fill (Criminisi style): the area is refilled patch by patch with the most similar real patch found
    around it, so the grain and the edges of the photo are copied, never smoothed. mask: 255 = remove."""
    ys, xs = np.nonzero(mask)
    y0, y1 = max(0, ys.min() - margin), min(img.shape[0], ys.max() + margin + 1)
    x0, x1 = max(0, xs.min() - margin), min(img.shape[1], xs.max() + margin + 1)
    pic = img[y0:y1, x0:x1].astype(np.float32)
    miss = mask[y0:y1, x0:x1] > 0
    known0 = ~miss
    h = patch // 2
    H, W = miss.shape
    conf = known0.astype(np.float32)
    # source patch centres: patches that lie completely on original (not removed) pixels and inside the crop
    full = cv2.erode(known0.astype(np.uint8), np.ones((patch, patch), np.uint8), borderType=cv2.BORDER_CONSTANT, borderValue=0) > 0
    full[:h, :] = full[-h:, :] = False
    full[:, :h] = full[:, -h:] = False
    from numpy.lib.stride_tricks import sliding_window_view
    wins = sliding_window_view(pic, (patch, patch, 3))[:, :, 0]          # [H-p+1, W-p+1, p, p, 3], index = top-left corner
    lum = pic.mean(axis=2)
    k = np.ones((patch, patch), np.uint8)
    wmax = cv2.dilate(lum, k)[h:H - h, h:W - h]                          # brightest / darkest pixel of every window
    wmin = cv2.erode(lum, k)[h:H - h, h:W - h]
    while miss.any():
        front = miss & (cv2.dilate((~miss).astype(np.uint8), np.ones((3, 3), np.uint8)) > 0)
        if not front.any():
            front = miss
        fy, fx = np.nonzero(front)
        fy_c, fx_c = np.clip(fy, h, H - h - 1), np.clip(fx, h, W - h - 1)
        score = np.array([conf[a - h:a + h + 1, b - h:b + h + 1].mean() for a, b in zip(fy_c, fx_c)])
        k = int(np.argmax(score))
        cy, cx = int(fy_c[k]), int(fx_c[k])
        t0y, t0x = cy - h, cx - h
        tgt = pic[t0y:t0y + patch, t0x:t0x + patch]
        tk = (~miss[t0y:t0y + patch, t0x:t0x + patch])[..., None].astype(np.float32)
        ya, yb = max(0, t0y - search), min(H - patch, t0y + search)
        xa, xb = max(0, t0x - search), min(W - patch, t0x + search)
        cand = wins[ya:yb + 1, xa:xb + 1]
        ssd = (((cand - tgt) ** 2) * tk).sum(axis=(2, 3, 4))
        ssd[~full[ya + h:yb + 1 + h, xa + h:xb + 1 + h]] = np.inf
        kn = tk[..., 0] > 0
        if kn.any():                                                     # no white / black fragments from neighbouring parts
            tl = tgt.mean(axis=2)[kn]
            ssd[(wmax[ya:yb + 1, xa:xb + 1] > tl.max() + 22) | (wmin[ya:yb + 1, xa:xb + 1] < tl.min() - 22)] = np.inf
        if not np.isfinite(ssd).any():
            miss[t0y:t0y + patch, t0x:t0x + patch] = False   # nothing to copy from: leave as is
            continue
        by, bx = np.unravel_index(int(np.argmin(ssd)), ssd.shape)
        src = pic[ya + by:ya + by + patch, xa + bx:xa + bx + patch]
        m = miss[t0y:t0y + patch, t0x:t0x + patch]
        pic[t0y:t0y + patch, t0x:t0x + patch][m] = src[m]
        conf[t0y:t0y + patch, t0x:t0x + patch][m] = score[k]
        miss[t0y:t0y + patch, t0x:t0x + patch] = False
    out = np.clip(pic, 0, 255).astype(np.uint8)
    region = mask[y0:y1, x0:x1] > 0
    img[y0:y1, x0:x1][region] = out[region]
    return img


def plain_cap(img, cx, cy, r_cap, r_mask, rng):
    """Replaces the logo on a round metal cap by a plain face: the colour of the cap rim around the logo, a gentle gradient
    (light from the upper left like in the photo) and the grain of the photo, so it stays sharp."""
    h, w = img.shape[:2]
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot(xx - cx, yy - cy)
    ring = (d > r_mask + 0.5) & (d <= r_cap)                       # untouched face around the logo
    face = d <= r_mask
    pts = np.stack([xx[ring], yy[ring]], 1).astype(np.float32)
    A = np.c_[np.ones(len(pts)), pts[:, 0] - cx, pts[:, 1] - cy]   # colour = c0 + cx*x + cy*y, fitted to the ring
    out = img.astype(np.float32)
    grain = rng.normal(0, 1.6, (h, w))                              # one grey grain for all channels (no colour noise)
    alpha = np.clip((r_mask + 1.2 - d) / 1.6, 0.0, 1.0)             # soft one-pixel edge towards the untouched rim
    for c in range(3):
        coef, *_ = np.linalg.lstsq(A, img[..., c][ring].astype(np.float32), rcond=None)
        fill = coef[0] + coef[1] * (xx - cx) + coef[2] * (yy - cy) + grain
        out[..., c] = out[..., c] * (1 - alpha) + fill * alpha
    box = d <= r_mask + 2
    img[box] = np.clip(out[box], 0, 255).astype(np.uint8)
    return box.astype(np.uint8) * 255


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    src = sys.argv[1]
    debug = "--debug" in sys.argv
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Source", "ui", "photos")
    os.makedirs(out_dir, exist_ok=True)
    rng = np.random.default_rng(7)
    for code, name in FILES.items():
        img = cv2.imread(os.path.join(src, name))
        if img is None:
            print("missing:", name)
            return 1
        if img.shape[1] != 1376 or img.shape[0] != 768:
            print("warning: %s is %dx%d, the coordinates were measured on 1376x768" % (name, img.shape[1], img.shape[0]))
        shown = np.zeros(img.shape[:2], np.uint8)
        caps = [e for e in EDITS.get(code, []) if e[0] == "cap"]
        polys = [e for e in EDITS.get(code, []) if e[0] == "poly"]
        for e in [e for e in EDITS.get(code, []) if e[0] == "shift"]:
            m = poly_mask(img.shape, e[1])
            shown |= m
            sh = np.roll(img, -e[2], axis=1)          # pixel at x takes the pixel from x + dx
            a = cv2.GaussianBlur(cv2.erode(m, np.ones((5, 5), np.uint8)).astype(np.float32) / 255, (0, 0), 2.0)[..., None]  # soft seam
            img = np.clip(img * (1 - a) + sh * a, 0, 255).astype(np.uint8)
        for e in polys:
            m = poly_mask(img.shape, e[1])
            shown |= m
            img = refill(img, cv2.dilate(m, np.ones((3, 3), np.uint8)))
        for _, cx, cy, rc, rm in caps:
            shown |= plain_cap(img, cx, cy, rc, rm, rng)
        # PIL: 4:4:4 chroma and quality 95, so that the colours of small details stay crisp
        Image.fromarray(cv2.cvtColor(img, cv2.COLOR_BGR2RGB)).save(
            os.path.join(out_dir, code + ".jpg"), quality=95, subsampling=0, optimize=True)
        if debug:
            vis = cv2.imread(os.path.join(src, name))
            vis[shown > 0] = (0, 0, 255)
            cv2.imwrite(os.path.join(out_dir, code + "_mask.png"), vis)
        print(code, "ok", "(edited)" if shown.any() else "")
    return 0


if __name__ == "__main__":
    sys.exit(main())
