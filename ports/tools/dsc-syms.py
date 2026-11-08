"""List symbols of one image in an iOS 7 armv7 dyld shared cache.

Usage: python3 -I tools/dsc-syms.py dyld_shared_cache_armv7 PATH_SUBSTRING [defined|undefined]
Reads LC_SYMTAB of the first image whose path contains PATH_SUBSTRING.
"""
import struct
import sys


def main(path, needle, which):
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

    for addr, _mt, _inode, pathoff, _pad in imgs:
        f.seek(pathoff)
        p = f.read(256).split(b"\0")[0].decode(errors="replace")
        if needle not in p:
            continue
        fo = fileoff(addr)
        f.seek(fo)
        mh = f.read(28)
        ncmds = struct.unpack_from("<I", mh, 16)[0]
        off = fo + 28
        for _ in range(ncmds):
            f.seek(off)
            cmd, size = struct.unpack("<II", f.read(8))
            if cmd == 2:
                f.seek(off + 8)
                symoff, nsyms, stroff, _ss = struct.unpack("<4I", f.read(16))
                print("#", p, "nsyms", nsyms)
                for i in range(nsyms):
                    f.seek(symoff + i * 12)
                    strx, typ, _sect, _desc, value = struct.unpack("<IBBhI", f.read(12))
                    if typ & 0xE0:
                        continue
                    undef = (typ & 0xE) == 0
                    if (which == "undefined") != undef:
                        continue
                    f.seek(stroff + strx)
                    name = f.read(128).split(b"\0")[0].decode(errors="replace")
                    print(name, hex(value))
            off += size
        return
    print("no image matches", needle)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else "defined")
