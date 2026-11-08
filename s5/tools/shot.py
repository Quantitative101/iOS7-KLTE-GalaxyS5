#!/usr/bin/env python3
"""Render the S5 GUI frame dump (IOS7S5SDHCI::dumpFrame) as a PNG, no third-party modules.

    adb shell 'dd if=/dev/block/mmcblk0p26 of=/tmp/shot.bin bs=512 skip=24761311 count=3001'; adb pull /tmp/shot.bin
    python3 -I s5/tools/shot.py shot.bin shot.png

Best frame (run 65+): same layout with "S5BEST01" and the score in word 7, 4096 sectors lower (losetup -o 12675694080).
Header sector: "S5SHOT01", then u32 frame count, width, height, row bytes, dump number. Pixels follow in BGRA.
Also prints the frame count, a few sample pixels and how much of the frame is non-black.
"""
import struct
import sys
import zlib

b = open(sys.argv[1], "rb").read()
if b[:8] not in (b"S5SHOT01", b"S5BEST01"):
    sys.exit("no S5SHOT01 header (no frame dumped yet?)")
count, w, h, row, dump = struct.unpack_from("<5I", b, 8)
px = b[512:512 + row * h]
lines = []
nonblack = 0
for y in range(h):
    r = px[y * row:y * row + w * 4]
    out = bytearray()
    for x in range(w):
        bb, g, rr, a = r[x * 4:x * 4 + 4]
        out += bytes((rr, g, bb))
        if rr or g or bb:
            nonblack += 1
    lines.append(b"\0" + bytes(out))


def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)


png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(b"".join(lines), 9)) + chunk(b"IEND", b"")
open(sys.argv[2], "wb").write(png)
samples = [(x, y, px[y * row + x * 4:y * row + x * 4 + 4].hex()) for (x, y) in ((0, 0), (240, 400), (479, 799), (240, 20))]
print("frame %u (dump %u) %ux%u row %u, non-black %.1f%%, samples %s" % (count, dump, w, h, row, 100.0 * nonblack / (w * h), samples))
