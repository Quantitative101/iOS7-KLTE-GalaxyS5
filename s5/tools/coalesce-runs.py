"""coalesce.py RUNS OUT GAP: merge "start count" runs separated by at most GAP sectors."""
import sys

runs = []
for line in open(sys.argv[1]):
    p = line.split()
    if len(p) == 2 and p[0].isdigit() and p[1].isdigit():
        runs.append((int(p[0]), int(p[1])))
gap = int(sys.argv[3])
out = []
for s, c in sorted(runs):
    if out and s - (out[-1][0] + out[-1][1]) <= gap:
        out[-1][1] = s + c - out[-1][0]
    else:
        out.append([s, c])
with open(sys.argv[2], 'w') as f:
    for s, c in out:
        f.write('%d %d\n' % (s, c))
print(len(runs), '->', len(out), 'runs,', sum(c for _, c in out), 'sectors')
