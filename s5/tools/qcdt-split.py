"""qcdt-split.py BOOT.IMG OUTDIR - write every DTB in a boot image's QCDT block as OUTDIR/dtb-<n>-<platform>-<variant>-<rev>.dtb.

QCDT (v1/v2/v3): "QCDT", version u32, count u32, then count entries of
  v1: platform, variant, soc_rev, offset, size
  v2: platform, variant, soc_rev, offset, size (+ subtype for v2? no: v2 adds subtype before soc_rev)
  v3: platform, variant, subtype, soc_rev, pmic0..3, offset, size
Offsets are relative to the start of the QCDT block. Identical offsets are written once.
"""
import os, struct, sys


def main(img, out):
    b = open(img, "rb").read()
    ksz, _, rsz, _, ssz, _, _, page, dtsz = struct.unpack_from("<9I", b, 8)
    pg = lambda n: (n + page - 1) // page
    off = page * (1 + pg(ksz) + pg(rsz) + pg(ssz))
    dt = b[off:off + dtsz]
    if dt[:4] != b"QCDT":
        sys.exit("no QCDT at 0x%x" % off)
    ver, cnt = struct.unpack_from("<II", dt, 4)
    words = {1: 5, 2: 6, 3: 10}[ver]
    os.makedirs(out, exist_ok=True)
    seen = {}
    for i in range(cnt):
        e = struct.unpack_from("<%dI" % words, dt, 12 + i * 4 * words)
        o, sz = e[-2], e[-1]
        plat, var = e[0], e[1]
        rev = e[3] if ver >= 2 else e[2]
        name = "dtb-%d-%x-%x-%x.dtb" % (i, plat, var, rev)
        print(i, "plat %x var %x rev %x off %x size %x" % (plat, var, rev, o, sz), "(dup of %s)" % seen[o] if o in seen else name)
        if o not in seen:
            open(os.path.join(out, name), "wb").write(dt[o:o + sz])
            seen[o] = name


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
