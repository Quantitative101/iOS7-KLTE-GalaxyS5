"""Map exported OpenGLES gl* stubs to their dispatch-table offsets.

Usage: python3 -I tools/gl-dispatch-map.py dyld_shared_cache_armv7 build/gles_syms.txt > build/gl-dispatch.txt
Each exported stub loads the context struct from TLS+0x78, passes ctx[0x10] as first argument and branches through ctx[off];
the Thumb-2 loads based on the dispatch register give `off`.
"""
import struct
import sys


def main(cache, symfile):
    f = open(cache, "rb")
    hdr = f.read(0x60)
    mapoff, mapcnt = struct.unpack_from("<II", hdr, 16)
    f.seek(mapoff)
    maps = [struct.unpack("<QQQII", f.read(32)) for _ in range(mapcnt)]

    def read(va, n):
        for a, sz, fo, _m, _i in maps:
            if a <= va < a + sz:
                f.seek(fo + va - a)
                return f.read(n)
        return b""

    out = []
    for line in open(symfile):
        parts = line.split()
        if len(parts) != 2 or not parts[0].startswith("_gl") or not parts[1].startswith("0x"):
            continue
        name, va = parts[0], int(parts[1], 16) & ~1
        code = read(va, 0x40)
        hw = struct.unpack("<%dH" % (len(code) // 2), code)
        offs = []
        i = 0
        base = None
        while i < len(hw) - 1:
            h = hw[i]
            if (h & 0xF800) == 0x6800:  # ldr Rt,[Rn,#imm5*4]
                rt, rn, imm = h & 7, (h >> 3) & 7, ((h >> 6) & 0x1F) * 4
                if imm == 0x78 and base is None:
                    base = rt
                elif base is not None and rn == base and imm != 0x10:
                    offs.append(imm)
                i += 1
            elif (h & 0xFFF0) == 0xF8D0:  # ldr.w Rt,[Rn,#imm12]
                rn, rt, imm = h & 0xF, (hw[i + 1] >> 12) & 0xF, hw[i + 1] & 0xFFF
                if imm == 0x78 and base is None:
                    base = rt
                elif base is not None and rn == base and imm != 0x10:
                    offs.append(imm)
                i += 2
            else:
                i += 1
        out.append((name, offs[0] if offs else None))
    for name, off in out:
        print(name, "0x%x" % off if off is not None else "?")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
