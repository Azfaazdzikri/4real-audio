#!/usr/bin/env python3
"""docs/manual/manual.html -> installer/4real-audio-Manual.pdf (Chromium via Playwright). The PDF is committed, so CI does not need this."""
import pathlib, sys
from playwright.sync_api import sync_playwright
root = pathlib.Path(__file__).resolve().parent.parent
src = root / "docs/manual/manual.html"
out = root / "installer/4real-audio-Manual.pdf"
foot = '<div style="width:100%;font-size:8px;color:#777;font-family:Arial;padding:0 18mm;display:flex;justify-content:space-between"><span>4real-audio User Manual</span><span>Page <span class="pageNumber"></span> / <span class="totalPages"></span></span></div>'
with sync_playwright() as p:
    b = p.chromium.launch(); pg = b.new_page()
    pg.goto(src.as_uri()); pg.wait_for_load_state("load")
    pg.pdf(path=str(out), format="A4", print_background=True, prefer_css_page_size=True, display_header_footer=True, header_template="<span></span>", footer_template=foot)
    b.close()
print("wrote", out)
