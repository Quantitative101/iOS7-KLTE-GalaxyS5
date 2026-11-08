"""Symbolize the J1SAMPLE blocks printed by the kernel sampler against the iOS 7 armv7 dyld shared cache.

The cache is mapped at a random slide each boot; the slide is recovered by locating the 8 raw words dumped at a
cache-resident PC inside the cache file. Usage: python3 -I tools/sample-sym.py serial.txt dyld_shared_cache_armv7 [proc]
Prints the last sample of every thread of `proc` (default: all sampled processes).
"""
import re
import struct
import sys

serial, cachepath = sys.argv[1], sys.argv[2]
want = sys.argv[3] if len(sys.argv) > 3 else None
data = open(cachepath, "rb").read()
mapoff, mapcnt, imgoff, imgcnt = struct.unpack_from("<4I", data, 16)
maps = [struct.unpack_from("<QQQII", data, mapoff + 32 * i) for i in range(mapcnt)]
imgs = sorted(struct.unpack_from("<QQQII", data, imgoff + 32 * i) for i in range(imgcnt))
CACHE_LO = min(m[0] for m in maps)
CACHE_HI = max(m[0] + m[1] for m in maps)


def cstr(off):
    return data[off:off + 200].split(b"\0")[0].decode(errors="replace")


def fileoff(va):
    for a, sz, fo, _p, _q in maps:
        if a <= va < a + sz:
            return fo + va - a
    return None


def find_slide(words, pc):
    needle = b"".join(struct.pack("<I", w) for w in words)
    i = data.find(needle)
    if i < 0:
        return None
    for a, sz, fo, _p, _q in maps:
        if fo <= i < fo + sz:
            return pc - (a + i - fo)
    return None


def symbol(va):
    cand = [im for im in imgs if im[0] <= va]
    if not cand:
        return "?"
    im = cand[-1]
    name = cstr(im[3]).split("/")[-1]
    fo = fileoff(im[0])
    best = None
    if fo is not None:
        ncmds = struct.unpack_from("<I", data, fo + 16)[0]
        off = fo + 28
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<II", data, off)
            if cmd == 2:
                symoff, nsyms, stroff, _ = struct.unpack_from("<4I", data, off + 8)
                for n in range(nsyms):
                    strx, t, _s, _d, v = struct.unpack_from("<IBBHI", data, symoff + n * 12)
                    if t & 0xE == 0xE and v <= va and (best is None or v > best[0]):
                        best = (v, strx)
                if best:
                    best = (best[0], cstr(stroff + best[1]))
                break
            off += size
    return "%s %s+0x%x" % (name, best[1], va - best[0]) if best else "%s +0x%x" % (name, va - im[0])


blocks = []
cur = None
for line in open(serial, errors="replace"):
    m = re.match(r"J1SAMPLE (\S+)\[(\d+)\] thr(\d+) state=0x(\w+) pc=(\w+) lr=(\w+) sp=(\w+)", line)
    if m:
        cur = {"proc": m.group(1), "pid": m.group(2), "thr": m.group(3), "state": m.group(4),
               "pc": int(m.group(5), 16), "lr": int(m.group(6), 16), "words": None, "frames": []}
        blocks.append(cur)
        continue
    if cur is None:
        continue
    m = re.match(r"J1SAMPLE\s+pc@(\w+):((?: [0-9a-f]{8}){8})", line)
    if m:
        cur["words"] = [int(w, 16) for w in m.group(2).split()]
        continue
    m = re.match(r"J1SAMPLE\s+frame \d+ ret=(\w+)", line)
    if m:
        cur["frames"].append(int(m.group(1), 16))

last = {}
for b in blocks:
    if want and b["proc"] != want:
        continue
    last[(b["pid"], b["thr"])] = b
slide = None
for b in last.values():
    if b["words"] and 0x30000000 <= b["pc"] < 0x40000000:
        slide = find_slide(b["words"], b["pc"] & ~1)
        if slide is not None:
            break
print("cache slide:", hex(slide) if slide is not None else "not found")
for key in sorted(last):
    b = last[key]
    print("%s[%s] thr%s state=%s" % (b["proc"], b["pid"], b["thr"], b["state"]))
    for label, a in [("pc", b["pc"])] + [("f%d" % i, f) for i, f in enumerate(b["frames"])]:
        a &= ~1
        if slide is not None and CACHE_LO <= a - slide < CACHE_HI:
            print("   %-3s %08x  %s" % (label, a, symbol(a - slide)))
        else:
            print("   %-3s %08x  (not in cache)" % (label, a))
