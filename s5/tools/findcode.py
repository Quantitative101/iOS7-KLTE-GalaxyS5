import struct
import sys

cache, words, pc_at = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)  # words: hex words dumped at pc_at
b = open(cache, 'rb').read()
pat = b''.join(struct.pack('<I', int(w, 16)) for w in words.split())
i = b.find(pat)
print('file offset', hex(i) if i >= 0 else None, 'count', b.count(pat))
if i < 0:
    sys.exit()
# mappings: dyld_cache_header mappingOffset at 0x10, count at 0x14; each {addr u64, size u64, fileoff u64, maxprot, initprot}
mo, mc = struct.unpack_from('<II', b, 0x10)
for k in range(mc):
    addr, size, fo, _, _ = struct.unpack_from('<QQQII', b, mo + k * 32)
    if fo <= i < fo + size:
        un = addr + (i - fo)
        print('unslid', hex(un), 'slide', hex(pc_at - un))
# images: imagesOffset 0x18, count 0x1c; each {addr u64, modTime u64, inode u64, pathFileOffset u32, pad}
io, ic = struct.unpack_from('<II', b, 0x18)
best = None
for k in range(ic):
    addr, _, _, po, _ = struct.unpack_from('<QQQII', b, io + k * 32)
    if addr <= un and (best is None or addr > best[0]):
        best = (addr, b[po:b.index(b'\0', po)].decode())
print('image', best[1], hex(best[0]), 'offset', hex(un - best[0]))
