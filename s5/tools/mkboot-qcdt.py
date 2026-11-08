#!/usr/bin/env python3
"""Clone a Qualcomm/Samsung boot image (Android header v0 with a QCDT block), replacing only the kernel.

Keeps the stock header fields (load addresses, page size, cmdline), the ramdisk, the second stage and the QCDT
(dt_size at header offset 0x28): LK on the S5 picks a DTB from the QCDT by msm-id and passes it to the kernel in r2.
Layout: header page | kernel | ramdisk | second | dt, each padded to the page size.

    mkboot-qcdt.py STOCK_BOOT_IMG NEW_KERNEL OUT_IMG
"""
import hashlib
import struct
import sys


def pages(n, page):
    return (n + page - 1) // page


def pad(b, page):
    return b + b"\0" * (-len(b) % page)


def main(stock, kernel, out):
    s = open(stock, "rb").read()
    if s[:8] != b"ANDROID!":
        sys.exit("not an Android boot image: %s" % stock)
    ksize, kaddr, rsize, raddr, ssize, saddr, taddr, page, dtsize = struct.unpack_from("<9I", s, 8)
    off = page
    off += pages(ksize, page) * page
    ramdisk = s[off:off + rsize]
    off += pages(rsize, page) * page
    second = s[off:off + ssize]
    off += pages(ssize, page) * page
    dt = s[off:off + dtsize]
    if len(dt) != dtsize or (dtsize and dt[:4] != b"QCDT"):
        sys.exit("QCDT block not found where expected (dt_size 0x%x at 0x%x)" % (dtsize, off))
    k = open(kernel, "rb").read()

    hdr = bytearray(s[:page])
    struct.pack_into("<I", hdr, 8, len(k))
    # id (offset 0x240, 32 bytes): SHA-1 over kernel, ramdisk, second (and dt) with their sizes, as mkbootimg does
    h = hashlib.sha1()
    for blob in (k, ramdisk, second, dt):
        h.update(blob)
        h.update(struct.pack("<I", len(blob)))
    hdr[0x240:0x260] = h.digest().ljust(32, b"\0")

    img = bytes(hdr) + pad(k, page) + pad(ramdisk, page) + pad(second, page) + pad(dt, page)
    open(out, "wb").write(img)
    print("wrote %s: kernel %d bytes @0x%x, ramdisk %d, dt %d (QCDT), page %d, total %d"
          % (out, len(k), kaddr, len(ramdisk), len(dt), page, len(img)))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
