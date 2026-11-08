"""objcsym.py CACHE IMAGE_SUBSTR UNSLID_ADDR...: name the ObjC method containing each address (armv7 dyld cache)."""
import struct
import sys

cache, sub = sys.argv[1], sys.argv[2]
targets = [int(a, 16) & ~1 for a in sys.argv[3:]]
b = open(cache, 'rb').read()
mo, mc = struct.unpack_from('<II', b, 0x10)
maps = [struct.unpack_from('<QQQII', b, mo + k * 32)[:3] for k in range(mc)]


def off(va):
    for addr, size, fo in maps:
        if addr <= va < addr + size:
            return fo + va - addr
    return None


def u32(va):
    o = off(va)
    return struct.unpack_from('<I', b, o)[0] if o is not None else 0


def cstr(va):
    o = off(va)
    if o is None:
        return '?'
    return b[o:b.index(b'\0', o)].decode('latin1')


io, ic = struct.unpack_from('<II', b, 0x18)
base = None
for k in range(ic):
    addr, _, _, po, _ = struct.unpack_from('<QQQII', b, io + k * 32)
    if sub in b[po:b.index(b'\0', po)].decode():
        base = addr
        break
o = off(base)
ncmds = struct.unpack_from('<I', b, o + 16)[0]
p = o + 28
sects = {}
for _ in range(ncmds):
    cmd, sz = struct.unpack_from('<II', b, p)
    if cmd == 1:  # LC_SEGMENT
        nsect = struct.unpack_from('<I', b, p + 48)[0]
        for s in range(nsect):
            q = p + 56 + s * 68
            name = b[q:q + 16].rstrip(b'\0').decode()
            addr, size = struct.unpack_from('<II', b, q + 32)
            sects[name] = (addr, size)
    p += sz
meths = []


def methods(ml, cls, meta):
    if not ml:
        return
    entsize, count = u32(ml) & 0xfffc, u32(ml + 4)
    for i in range(count):
        e = ml + 8 + i * entsize
        meths.append((u32(e + 8) & ~1, ('+' if meta else '-') + '[%s %s]' % (cls, cstr(u32(e)))))


ca, cs = sects['__objc_classlist']
for i in range(cs // 4):
    cls = u32(ca + i * 4)
    for meta in (0, 1):
        c = u32(cls) if meta else cls  # isa -> metaclass
        ro = u32(c + 16) & ~3
        name = cstr(u32(ro + 16))
        methods(u32(ro + 20), name, meta)
if '__objc_catlist' in sects:
    ca, cs = sects['__objc_catlist']
    for i in range(cs // 4):
        cat = u32(ca + i * 4)
        cname = cstr(u32(cat)) + '(' + cstr(u32(u32(cat + 4) + 16) if u32(cat + 4) else 0) + ')'
        methods(u32(cat + 8), cname, 0)
        methods(u32(cat + 12), cname, 1)
meths.sort()
for t in targets:
    best = max((m for m in meths if m[0] <= t), default=None)
    print(hex(t), best[1] if best else None, hex(t - best[0]) if best else '')
