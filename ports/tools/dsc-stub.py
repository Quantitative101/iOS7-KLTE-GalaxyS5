"""Name the imported function behind a symbol stub in an iOS 7 armv7 dyld shared cache image.

Usage: python3 -I tools/dsc-stub.py dyld_shared_cache_armv7 0x386dec30 [more stub addresses]
Finds the containing image, walks its sections for the symbol-stub section holding the address and resolves the entry
through the indirect symbol table + LC_SYMTAB.
"""
import struct
import sys


def main(path, addrs):
    f = open(path, "rb")
    hdr = f.read(0x60)
    mapoff, mapcnt, imgoff, imgcnt = struct.unpack_from("<4I", hdr, 16)
    f.seek(mapoff)
    maps = [struct.unpack("<QQQII", f.read(32)) for _ in range(mapcnt)]
    f.seek(imgoff)
    imgs = [struct.unpack("<QQQII", f.read(32)) for _ in range(imgcnt)]

    def fileoff(va):
        for a, sz, fo, _mp, _ip in maps:
            if a <= va < a + sz:
                return fo + va - a
        return None

    def cstr(off):
        f.seek(off)
        return f.read(256).split(b"\0")[0].decode(errors="replace")

    imgs.sort(key=lambda i: i[0])
    for va in map(lambda s: int(s, 0), addrs):
        img = [i for i in imgs if i[0] <= va][-1]
        fo = fileoff(img[0])
        f.seek(fo)
        mh = f.read(28)
        ncmds = struct.unpack_from("<I", mh, 16)[0]
        off = fo + 28
        sects = []
        symoff = nsyms = stroff = indoff = nind = 0
        for _ in range(ncmds):
            f.seek(off)
            cmd, size = struct.unpack("<II", f.read(8))
            if cmd == 1:  # LC_SEGMENT
                f.seek(off + 8)
                body = f.read(48)
                nsects = struct.unpack_from("<I", body, 40)[0]
                for s in range(nsects):
                    sec = f.read(68)
                    name = sec[:16].split(b"\0")[0].decode()
                    addr, ssize, _o, _al, _ro, _nr, flags, r1, r2 = struct.unpack_from("<IIIIIIIII", sec, 32)
                    sects.append((name, addr, ssize, flags, r1, r2))
            elif cmd == 2:
                f.seek(off + 8)
                symoff, nsyms, stroff, _ss = struct.unpack("<4I", f.read(16))
            elif cmd == 0xB:  # LC_DYSYMTAB
                f.seek(off + 8)
                d = struct.unpack("<18I", f.read(72))
                indoff, nind = d[12], d[13]
            off += size
        for name, addr, ssize, flags, r1, r2 in sects:
            if addr <= va < addr + ssize and (flags & 0xff) in (8, 7, 6) and r2:
                idx = r1 + (va - addr) // r2
                f.seek(indoff + idx * 4)
                sym = struct.unpack("<I", f.read(4))[0]
                f.seek(symoff + sym * 12)
                strx = struct.unpack("<I", f.read(4))[0]
                print("0x%x (%s): %s" % (va, name, cstr(stroff + strx)))
                break
        else:
            print("0x%x: not in a stub section (sections: %s)" % (va, [(s[0], hex(s[1])) for s in sects if s[1] <= va < s[1] + s[2]]))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
