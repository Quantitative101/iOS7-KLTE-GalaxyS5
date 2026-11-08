#!/usr/bin/env python3
"""Make @1x PNGs from the @2x-only artwork of the retina iPod5,1 root, for the S5's 480x800 @1x display.

    python3 -I s5/tools/make-1x-images.py ROOT OUT DIR [DIR ...]

ROOT is the mounted root volume, DIR paths are relative to it (e.g. Applications System/Library/CoreServices/SpringBoard.app).
For every NAME@2x[~dev].png under DIR (one level of bundle nesting is walked fully) with no NAME[~dev].png beside it, a
half-size standard PNG is written to OUT/<same relative path>. Apple's CgBI PNGs (raw deflate, BGRA, premultiplied) are
decoded here; the output is an ordinary RGBA PNG, which UIKit loads too.
"""
import io
import os
import struct
import sys
import zlib

from PIL import Image


def chunks(data):
    pos = 8
    while pos + 8 <= len(data):
        n, t = struct.unpack(">I4s", data[pos:pos + 8])
        yield t, data[pos + 8:pos + 8 + n]
        pos += 12 + n


def unfilter(raw, w, h, bpp):
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif ft == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    return bytes(out)


def load(path):
    data = open(path, "rb").read()
    if data[12:16] != b"CgBI":
        im = Image.open(io.BytesIO(data))
        im.load()
        return im.convert("RGBA")
    ihdr = None
    idat = b""
    for t, c in chunks(data):
        if t == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", c)
        elif t == b"IDAT":
            idat += c
    w, h, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError("unsupported CgBI %s" % (ihdr,))
    bpp = 4 if ctype == 6 else 3
    px = unfilter(zlib.decompress(idat, -15), w, h, bpp)
    if bpp == 3:  # BGR
        return Image.frombytes("RGB", (w, h), px, "raw", "BGR").convert("RGBA")
    # BGRA premultiplied: un-premultiply into RGBA
    im = Image.frombytes("RGBa", (w, h), px, "raw", "BGRa")
    return im.convert("RGBA")


def main():
    root, out, dirs = sys.argv[1], sys.argv[2], sys.argv[3:]
    made = skipped = failed = 0
    for d in dirs:
        for dp, _, files in os.walk(os.path.join(root, d)):
            names = set(files)
            for f in files:
                if "@2x" not in f or not f.endswith(".png"):
                    continue
                one = f.replace("@2x", "", 1)
                if one in names:
                    skipped += 1
                    continue
                src = os.path.join(dp, f)
                rel = os.path.relpath(os.path.join(dp, one), root)
                dst = os.path.join(out, rel)
                try:
                    im = load(src)
                    w, h = im.size
                    small = im.resize((max(1, (w + 1) // 2), max(1, (h + 1) // 2)), Image.LANCZOS)
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    small.save(dst, optimize=True)
                    made += 1
                except Exception as e:  # keep going; report at the end
                    print("FAIL %s: %s" % (src, e), file=sys.stderr)
                    failed += 1
    print("made %d, already had 1x %d, failed %d" % (made, skipped, failed))


if __name__ == "__main__":
    main()
