#!/usr/bin/env python3
"""Decode the S5 kernel log dump (raw ring copied by IOS7S5SDHCI to the last 1024 sectors of userdata) into text.

    adb shell 'dd if=/dev/block/mmcblk0p26 of=/tmp/ring.bin bs=512 skip=24765407 count=520'; adb pull /tmp/ring.bin
    python3 -I s5/tools/ringbin.py ring.bin

Same ring layout as the S4 Mini (tools/s4mini/ringdump.py): word0 'J1LG', word1 = bytes ever written, text from byte
16; the first half is the boot (linear), the second half is circular over the latest output.
"""
import struct
import sys

CAP = 0x40000
b = open(sys.argv[1], "rb").read()
if len(b) < 16 or struct.unpack_from("<I", b, 0)[0] != 0x474C314A:
    sys.exit("no J1LG header")
total = struct.unpack_from("<I", b, 4)[0]
raw = b[16:16 + CAP]
half = CAP // 2
if total <= half:
    data = raw[:total]
else:
    head = raw[:half]
    tail = raw[half:CAP]
    n = total - half
    if n <= half:
        data = head + tail[:n]
    else:
        p = n % half
        data = head + b"\n[... ring wrapped ...]\n" + tail[p:] + tail[:p]
sys.stdout.write("RING: %d bytes written\n" % total)
sys.stdout.write(data.decode("latin-1"))
