#!/usr/bin/env python3
# Downscale screenshots to web-friendly height (default 720p) with high-quality Lanczos.
# Usage: python3 downscale.py [in_dir] [out_dir] [target_height] [--jpg] [--sharpen]
#   preserves aspect ratio (16:9 1440p -> 1280x720); never upscales; skips already-small images.
#   PNG stays PNG (crisp UI/text), JPG stays JPG q92, BMP/TGA -> PNG. --jpg forces JPG for everything.
import sys, os
from PIL import Image, ImageFilter

IN  = sys.argv[1] if len(sys.argv) > 1 else "raw"
OUT = sys.argv[2] if len(sys.argv) > 2 else "web"
H   = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3].isdigit() else 720
FORCE_JPG = "--jpg" in sys.argv
SHARPEN   = "--sharpen" in sys.argv
EXT = {".png",".jpg",".jpeg",".bmp",".tga",".webp",".tif",".tiff"}

os.makedirs(OUT, exist_ok=True)
n = 0
for f in sorted(os.listdir(IN)):
    stem, ext = os.path.splitext(f)
    if ext.lower() not in EXT: continue
    im = Image.open(os.path.join(IN, f))
    w0, h0 = im.size
    if h0 <= H:
        print(f"  skip  {f}  ({w0}x{h0} already <= {H}p)"); continue
    w = round(w0 * H / h0)
    im = im.convert("RGB").resize((w, H), Image.LANCZOS)
    if SHARPEN:
        im = im.filter(ImageFilter.UnsharpMask(radius=1.0, percent=55, threshold=1))
    if FORCE_JPG or ext.lower() in {".bmp",".tga"} and False:
        out_ext = ".jpg"
    elif ext.lower() in {".jpg",".jpeg"}:
        out_ext = ".jpg"
    elif ext.lower() in {".bmp",".tga",".tif",".tiff"}:
        out_ext = ".png"
    else:
        out_ext = ext.lower()
    if FORCE_JPG: out_ext = ".jpg"
    outp = os.path.join(OUT, stem + out_ext)
    if out_ext == ".jpg":
        im.save(outp, "JPEG", quality=92, optimize=True, progressive=True)
    else:
        im.save(outp, "PNG", optimize=True)
    print(f"  {f}  {w0}x{h0} -> {w}x{H}  -> {os.path.basename(outp)}")
    n += 1
print(f"done: {n} image(s) -> {OUT}/")
