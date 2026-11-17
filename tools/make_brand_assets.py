#!/usr/bin/env python3
"""Makes every logo file of 4real-audio from ONE picture (the logo on a dark background).

  python3 tools/make_brand_assets.py <logo picture> [--repo <repo root>]

Writes (all of them are committed, so this only has to run when the logo changes; needs Pillow and numpy):
  assets/brand/logo_original.png          the picture as given
  assets/brand/icon_1024.png icon_256.png app icon (rounded square, dark): Standalone .exe / .app (CMake: ICON_BIG / ICON_SMALL)
  Source/ui/brand/logo_header.png         the logo with a transparent background, drawn in the plugin header
  installer/windows/4real.ico             icon of Setup.exe and of the uninstaller
  installer/windows/wizard_*.bmp          pictures in the Windows installer wizard (side picture and small corner picture)
  installer/macos/pkg_background.png      faint logo behind the macOS installer pages
"""
import argparse
import pathlib
import shutil

import numpy as np
from PIL import Image, ImageDraw

BG = (21, 23, 28)  # = the plugin background 0xff15171c


def keyed_logo(src: Image.Image) -> Image.Image:
    """RGBA logo cut out of its dark background and cropped to the artwork."""
    px = np.asarray(src.convert("RGB")).astype(np.float32)
    bright = px.max(axis=2)
    alpha = np.clip((bright - 45.0) / (125.0 - 45.0), 0.0, 1.0)
    bg = np.array(BG, np.float32)
    safe = np.maximum(alpha, 1e-3)[..., None]
    colour = np.clip((px - bg * (1.0 - safe)) / safe, 0, 255)
    rgba = np.dstack([colour, alpha * 255.0]).astype(np.uint8)
    img = Image.fromarray(rgba, "RGBA")
    box = img.getchannel("A").point(lambda v: 255 if v > 40 else 0).getbbox()
    pad = 6
    box = (max(0, box[0] - pad), max(0, box[1] - pad), min(img.width, box[2] + pad), min(img.height, box[3] + pad))
    return img.crop(box)


def fit(img: Image.Image, w: int, h: int) -> Image.Image:
    scale = min(w / img.width, h / img.height)
    return img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), Image.LANCZOS)


def rounded_icon(logo: Image.Image, size: int) -> Image.Image:
    """Dark rounded square (Big Sur style: the body fills 80 percent of the canvas) with the logo in the middle."""
    big = size * 4
    body = round(big * 0.80)
    canvas = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    mask = Image.new("L", (big, big), 0)
    off = (big - body) // 2
    ImageDraw.Draw(mask).rounded_rectangle((off, off, off + body, off + body), radius=round(body * 0.225), fill=255)
    canvas.paste(Image.new("RGBA", (big, big), BG + (255,)), (0, 0), mask)
    mark = fit(logo, round(body * 0.84), round(body * 0.84))
    canvas.alpha_composite(mark, ((big - mark.width) // 2, (big - mark.height) // 2))
    return canvas.resize((size, size), Image.LANCZOS)


def tight_icon(logo: Image.Image, size: int) -> Image.Image:
    """Windows icon: the dark square fills the whole canvas and the logo fills the square, so the headstock and the
    gold letters stay as large as possible at 16-48 px (where the Explorer list shows it)."""
    big = size * 8
    canvas = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, big - 1, big - 1), radius=round(big * 0.16), fill=255)
    canvas.paste(Image.new("RGBA", (big, big), BG + (255,)), (0, 0), mask)
    mark = fit(logo, round(big * 0.94), big)
    canvas.alpha_composite(mark, ((big - mark.width) // 2, (big - mark.height) // 2))
    out = canvas.resize((size, size), Image.LANCZOS)
    if size <= 48:
        from PIL import ImageFilter
        out = out.filter(ImageFilter.UnsharpMask(radius=0.8, percent=90, threshold=0))
    return out


def on_dark(logo: Image.Image, w: int, h: int, logo_w: float, y_frac: float) -> Image.Image:
    img = Image.new("RGB", (w, h), BG)
    mark = fit(logo, round(w * logo_w), h)
    rgba = img.convert("RGBA")
    rgba.alpha_composite(mark, ((w - mark.width) // 2, round(h * y_frac - mark.height / 2)))
    return rgba.convert("RGB")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("logo")
    ap.add_argument("--repo", default=str(pathlib.Path(__file__).resolve().parent.parent))
    a = ap.parse_args()
    repo = pathlib.Path(a.repo)
    brand, ui, win, mac = repo / "assets/brand", repo / "Source/ui/brand", repo / "installer/windows", repo / "installer/macos"
    for d in (brand, ui, win, mac):
        d.mkdir(parents=True, exist_ok=True)

    src = Image.open(a.logo)
    if pathlib.Path(a.logo).resolve() != (brand / "logo_original.png").resolve():
        shutil.copyfile(a.logo, brand / "logo_original.png")
    logo = keyed_logo(src)

    # in the plugin header (drawn about 44 px high; 4x for retina screens)
    header = fit(logo, 4000, 176)
    header.save(ui / "logo_header.png", optimize=True)

    # app icons
    icon = rounded_icon(logo, 1024)
    icon.save(brand / "icon_1024.png", optimize=True)
    rounded_icon(logo, 256).save(brand / "icon_256.png", optimize=True)
    tight_icon(logo, 256).save(win / "4real.ico", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)],
                               append_images=[tight_icon(logo, s) for s in (16, 20, 24, 32, 40, 48, 64, 128)])

    # Windows installer wizard (BMP, two sizes each for high-dpi screens)
    for w, h in ((164, 314), (328, 628)):
        on_dark(logo, w, h, 0.96, 0.46).save(win / f"wizard_large_{w}x{h}.bmp")
    for s in (55, 110):
        rounded_icon(logo, s).convert("RGBA").save(win / f"wizard_small_{s}.png")
        bg = Image.new("RGB", (s, s), (255, 255, 255))
        small = tight_icon(logo, s)
        bg.paste(small, (0, 0), small)
        bg.save(win / f"wizard_small_{s}.bmp")
        (win / f"wizard_small_{s}.png").unlink()

    # macOS installer: faint logo at the bottom left
    faint = fit(logo, 420, 300)
    alpha = faint.getchannel("A").point(lambda v: round(v * 0.22))
    faint.putalpha(alpha)
    bgimg = Image.new("RGBA", (520, 340), (0, 0, 0, 0))
    bgimg.alpha_composite(faint, (20, 340 - faint.height - 16))
    bgimg.save(mac / "pkg_background.png", optimize=True)

    print("cropped logo", logo.size, "-> header", header.size)


if __name__ == "__main__":
    main()
