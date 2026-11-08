"""secdiff.py OLD NEW - list runs of differing 512-byte sectors between two equal-size images: 'start count'."""
import mmap, sys

a = open(sys.argv[1], "rb")
b = open(sys.argv[2], "rb")
ma = mmap.mmap(a.fileno(), 0, access=mmap.ACCESS_READ)
mb = mmap.mmap(b.fileno(), 0, access=mmap.ACCESS_READ)
assert len(ma) == len(mb)
CH = 1 << 20
runs = []
for o in range(0, len(ma), CH):
    if ma[o:o + CH] == mb[o:o + CH]:
        continue
    for s in range(o, min(o + CH, len(ma)), 512):
        if ma[s:s + 512] != mb[s:s + 512]:
            sec = s // 512
            if runs and runs[-1][0] + runs[-1][1] == sec:
                runs[-1][1] += 1
            else:
                runs.append([sec, 1])
for st, n in runs:
    print(st, n)
print("total sectors", sum(n for _, n in runs), "runs", len(runs), file=sys.stderr)
