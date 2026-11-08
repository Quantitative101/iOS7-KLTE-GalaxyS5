"""Assemble a QEMU test disk: MBR with two type-0xAF (Apple_HFS) partitions, root volume then /private/var volume.

The kernel's IOFDiskPartitionScheme maps 0xAF to Apple_HFS and names the partitions disk0s1 / disk0s2, which is what
the iOS /etc/fstab expects.
Usage: python3 -I tools/make-disk.py root.hfs var.hfs disk.img
"""
import os
import struct
import sys

ALIGN = 2048  # sectors (1 MiB)


def main(root, var, out):
    sizes = [os.path.getsize(root), os.path.getsize(var)]
    starts = [ALIGN]
    starts.append(starts[0] + (sizes[0] // 512 + ALIGN - 1) // ALIGN * ALIGN)
    mbr = bytearray(512)
    for i in range(2):
        e = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", 0xAF, b"\xfe\xff\xff", starts[i], sizes[i] // 512)
        mbr[446 + 16 * i:446 + 16 * (i + 1)] = e
    # third partition: 2 MiB payload area for tools/gl-push.py (read live by the guest as /dev/rdisk0s3)
    pstart = starts[1] + (sizes[1] // 512 + ALIGN - 1) // ALIGN * ALIGN
    mbr[446 + 32:446 + 48] = struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", 0xAF, b"\xfe\xff\xff", pstart, 4096)
    mbr[510:512] = b"\x55\xaa"
    with open(out, "wb") as f:
        f.write(mbr)
        for path, start in zip((root, var), starts):
            f.seek(start * 512)
            with open(path, "rb") as src:
                while True:
                    buf = src.read(1 << 22)
                    if not buf:
                        break
                    f.write(buf)
        f.truncate((pstart + 4096) * 512)
    print("disk: p1 start=%d sectors=%d, p2 start=%d sectors=%d" % (starts[0], sizes[0] // 512, starts[1], sizes[1] // 512))


if __name__ == "__main__":
    main(*sys.argv[1:4])
