"""Extract the HFS/HFSX volume from a raw Apple-partition-map disk image into a bare volume image.

Usage: python3 -I tools/extract-hfs.py disk.raw out.hfs
Apple partition map: 512-byte blocks, entry n at block n, signature 'PM'; be32 start block @8, block count @12, type @48.
"""
import struct
import sys


def main(src, dst):
    f = open(src, "rb")
    ddm = f.read(512)
    assert ddm[:2] == b"ER", "no driver descriptor map"
    bs = struct.unpack(">H", ddm[2:4])[0] or 512
    print("map block size", bs)
    f.seek(bs)
    first = f.read(512)
    assert first[:2] == b"PM", "no Apple partition map"
    nparts = struct.unpack(">I", first[4:8])[0]
    chosen = None
    for n in range(1, nparts + 1):
        f.seek(bs * n)
        ent = f.read(512)
        start, count = struct.unpack(">II", ent[8:16])
        ptype = ent[48:80].split(b"\0")[0].decode()
        print("partition %d: %s start=%d blocks=%d" % (n, ptype, start, count))
        if ptype in ("Apple_HFS", "Apple_HFSX") and chosen is None:
            chosen = (start, count)
    assert chosen, "no HFS partition"
    start, count = chosen
    f.seek(start * bs)
    out = open(dst, "wb")
    left = count * bs
    while left:
        buf = f.read(min(left, 1 << 22))
        if not buf:
            break
        out.write(buf)
        left -= len(buf)
    out.close()
    f.seek(start * bs + 1024)
    print("volume signature:", f.read(4))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
