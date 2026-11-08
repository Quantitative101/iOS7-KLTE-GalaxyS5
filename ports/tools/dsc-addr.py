"""Name the dylib and nearest symbol for addresses inside an iOS 7 (v1) armv7 dyld shared cache.

Usage: python3 -I tools/dsc-addr.py dyld_shared_cache_armv7 0x39856442 [more addresses]
Reads the cache header, finds the containing image, and looks up the nearest local/global symbol from that image's
LC_SYMTAB (symbol/string table offsets are file offsets within the cache).
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
        cand = [i for i in imgs if i[0] <= va]
        if not cand:
            print("0x%x: before first image" % va)
            continue
        img = cand[-1]
        name = cstr(img[3])
        best = None
        fo = fileoff(img[0])
        if fo is not None:
            f.seek(fo)
            mh = f.read(28)
            ncmds = struct.unpack_from("<I", mh, 16)[0]
            off = fo + 28
            for _ in range(ncmds):
                f.seek(off)
                cmd, size = struct.unpack("<II", f.read(8))
                if cmd == 2:  # LC_SYMTAB
                    f.seek(off + 8)
                    symoff, nsyms, stroff, _strsize = struct.unpack("<4I", f.read(16))
                    f.seek(symoff)
                    raw = f.read(nsyms * 12)
                    for n in range(nsyms):
                        strx, ntype, _sect, _desc, value = struct.unpack_from("<IBBHI", raw, n * 12)
                        if ntype & 0xE == 0xE and value <= va and (best is None or value > best[0]):
                            best = (value, strx)
                    if best:
                        best = (best[0], cstr(stroff + best[1]))
                    break
                off += size
        if best:
            print("0x%x: %s  %s+0x%x" % (va, name, best[1], va - best[0]))
        else:
            print("0x%x: %s (+0x%x), no symbol" % (va, name, va - img[0]))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
