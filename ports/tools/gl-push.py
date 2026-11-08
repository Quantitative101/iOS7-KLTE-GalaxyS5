"""Deliver a payload to the running (or next-boot) guest through the payload partition (disk0s3) of disk.img.

Usage: python3 -I tools/gl-push.py build/disk.img glshim/j1gl.dylib
tools/make-disk.py adds a third 2 MiB MBR partition. Layout inside it: sector 0 = header {'J1GL', seq u32, len u32},
payload from sector 1 (<= 2 MiB - 512). QEMU runs with snapshot=on, so unwritten sectors are read live from this base
file; the guest reload daemon (testapp/fbtest.c, hijacked fairplayd.A1) polls the header through /dev/rdisk0s3. Data is
written first and the header last, so the guest never sees a half-written payload.
"""
import struct
import sys
import time


def main(img, payload):
    data = open(payload, "rb").read()
    with open(img, "r+b") as f:
        f.seek(446 + 32)
        _boot, _chs, ptype, _chs2, start, count = struct.unpack("<B3sB3sII", f.read(16))
        if ptype != 0xAF or count < 4096:
            raise SystemExit("no payload partition in %s: rebuild the disk with tools/make-disk.py" % img)
        if len(data) > count * 512 - 512:
            raise SystemExit("payload too large")
        seq = (int(time.time()) & 0x7FFFFFFF) or 1
        f.seek(start * 512 + 512)
        f.write(data + b"\0" * (-len(data) % 512))
        f.flush()
        f.seek(start * 512)
        f.write(struct.pack("<4sII", b"J1GL", seq, len(data)) + b"\0" * (512 - 12))
        f.flush()
    print("pushed %d bytes to sector %d, seq %d" % (len(data), start, seq))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
