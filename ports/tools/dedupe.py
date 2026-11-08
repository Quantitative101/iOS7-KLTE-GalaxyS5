"""Collapse the kernel console's doubled characters (UART + panel console both print) back to single text.
Usage: python3 -I tools/dedupe.py serial.log [lines-to-show-from-end]
"""
import sys

out = []
for line in open(sys.argv[1], errors="replace").read().split("\n"):
    d = line[0::2] if len(line) > 3 and all(line[i] == line[i + 1] for i in range(0, len(line) - 1, 2)) else line
    out.append(d)
n = int(sys.argv[2]) if len(sys.argv) > 2 else len(out)
print("\n".join(l for l in out[-n:] if l.strip()))
