"""Convert a raw 480x800 BGRA framebuffer dump (row bytes 1920) to a PNG using only the standard library.
Usage: python3 -I tools/fb2png.py fb.bin fb.png
"""
import struct
import sys
import zlib

W, H, ROW = 480, 800, 1920


def main(src, dst):
    raw = open(src, "rb").read()
    rows = []
    for y in range(H):
        line = raw[y * ROW:y * ROW + W * 4]
        rgb = bytearray()
        for x in range(W):
            b, g, r = line[x * 4], line[x * 4 + 1], line[x * 4 + 2]
            rgb += bytes((r, g, b))
        rows.append(b"\x00" + bytes(rgb))
    body = zlib.compress(b"".join(rows), 6)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) + chunk(b"IDAT", body) + chunk(b"IEND", b"")
    open(dst, "wb").write(png)
    nonzero = sum(1 for i in range(0, len(raw), 4) if raw[i:i + 3] != b"\0\0\0")
    print("wrote", dst, "nonblack pixels:", nonzero)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
