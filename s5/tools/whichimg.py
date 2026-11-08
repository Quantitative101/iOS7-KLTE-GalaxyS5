import struct
import sys

cache, slide = sys.argv[1], int(sys.argv[2], 16)
b = open(cache, 'rb').read()
io, ic = struct.unpack_from('<II', b, 0x18)
imgs = []
for k in range(ic):
    addr, _, _, po, _ = struct.unpack_from('<QQQII', b, io + k * 32)
    imgs.append((addr, b[po:b.index(b'\0', po)].decode()))
imgs.sort()
for a in sys.argv[3:]:
    un = int(a, 16) - slide
    best = max((x for x in imgs if x[0] <= un), default=None)
    print(a, hex(un), best[1].split('/')[-1], hex(un - best[0]))
