#!/usr/bin/env python3
"""Decode renderer profile lines from a decoded ring log (s5/tools/ringbin.py output).

  python3 s5/tools/prof.py s5/logs/ringNNN.txt

PROF (backboardd shim, every 32 frames): window ticks, draw, rect, rect-rejected, tri, clear, tex, readpixels ticks,
then draws, rects, tris, rect pixels, clear pixels, texture pixels, frame counter. S5GUI ... prof P I (kernel, every 64
frames): ticks in present (copy + panel mirror) and between presents. Ticks are mach_absolute_time (24 MHz on the S5).
"""
import re
import sys

TICK_MS = 1 / 1000000.0  # mach_absolute_time is in ns on this port (run 119: 217M mach ticks = 4.17M ticks of the 19.2 MHz CNTPCT)
NAMES = ["draw", "rect", "rectno", "tri", "clear", "tex", "read"]

for line in open(sys.argv[1], errors="replace"):
    m = re.search(r"PROF ((?:[0-9a-f]{8} ){15})", line)
    if m:
        v = [int(x, 16) for x in m.group(1).split()]
        win = v[0] * TICK_MS / 32
        parts = ", ".join("%s %.1f" % (n, v[1 + i] * TICK_MS / 32) for i, n in enumerate(NAMES))
        print("frame %5d: %.1f ms/frame (%.1f fps) | %s | outside GL %.1f | per frame: %d draws %d rects %d tris, "
              "%d rect px, %d clear px, %d tex px"
              % (v[14], win, 1000 / win if win else 0, parts, win - v[1] * TICK_MS / 32 - sum(v[5:8]) * TICK_MS / 32,
                 v[8] // 32, v[9] // 32, v[10] // 32, v[11] // 32, v[12] // 32, v[13] // 32))
        continue
    m = re.search(r"S5GUI frame ([0-9a-f]{8}) [0-9a-f]{8} prof ([0-9a-f]{8}) ([0-9a-f]{8})((?: [0-9a-f]{8}){5})?", line)
    if m:
        f, p, i = (int(x, 16) for x in m.groups()[:3])
        if f >= 64 and i:
            extra = ""
            if m.group(4):
                h, e, w, rows, c = (int(x, 16) for x in m.group(4).split())
                extra = (" | hash %.1f, expand %.1f, panel write %.1f, shadow copy %.1f ms/frame, %d changed rows/frame"
                         % (h * TICK_MS / 64, e * TICK_MS / 64, w * TICK_MS / 64, c * TICK_MS / 64, rows // 64))
            print("kernel frame %5d: present+mirror %.1f ms/frame, present interval %.1f ms (%.1f presents/s)%s"
                  % (f, p * TICK_MS / 64, i * TICK_MS / 64, 64000 / (i * TICK_MS), extra))
