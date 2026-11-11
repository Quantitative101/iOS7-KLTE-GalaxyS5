#!/usr/bin/env python3
"""Apply the S5 byte patches to Apple binaries from the iPod5,1 iOS 7.1.2 (11D257) root filesystem.

    python3 -I s5/tools/patch-ipod-binaries.py springboard IN OUT
    python3 -I s5/tools/patch-ipod-binaries.py dyld-cache  IN OUT

IN must be the unmodified file from the 11D257 iPod5,1 root (checked by SHA-1 before anything is written); OUT is the
patched copy (also checked). Only offsets and a few instruction bytes are listed here, no Apple code.

SpringBoard (/System/Library/CoreServices/SpringBoard.app/SpringBoard), Thumb code, file offsets:
  0x0035f8  beq -> b (21 d0 -> 21 e0): skip -[SBAppSwitcherStatusBarViewCache preheatStatusBarCache], which crashed in
            a fast enumeration with a NULL mutations pointer.
  0x1db900  bx lr: -[SBThermalController startListeningForThermalEvents] (no thermalmonitord, so SpringBoard showed
            the "needs to cool down" screen).
  0x228040  bx lr: -[SBBacklightController _autoLockTimerFired:]
  0x2280f8  bx lr: -[SBBacklightController _didIdle]
  0x2281fc  bx lr: -[SBBacklightController _lockScreenDimTimerFired]
            (the idle timers turned the display off; the panel kept the last frame and touch stopped working)

dyld shared cache (/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7), file offsets:
  0x218e780, 0x218e908  bx lr (was push {r4-r7, lr}): two IOKit.framework functions (unslid 0x2e18e780 / 0x2e18e908)
            that made powerd abort() and respawn in a loop on this hardware.
  0x0b2159fe  libMobileGestalt: mov.w r1,#-1 / mov.w r2,#-1 -> movs r1,#0; nop / movs r2,#0; nop (a wait with an
            infinite timeout becomes a poll).
"""
import hashlib
import sys

BX_LR = bytes.fromhex("7047")

PATCHES = {
    "springboard": {
        "in": "dceefa075fc26a63bf7af02df2bb5e00c212c228",
        "out": "defb4dfc0896b0012ff3da02bf87d36e4eaf84d1",
        "edits": [
            (0x0035F9, bytes.fromhex("e0")),
            (0x1DB900, BX_LR),
            (0x228040, BX_LR),
            (0x2280F8, BX_LR),
            (0x2281FC, BX_LR),
        ],
    },
    "dyld-cache": {
        "in": "9bbf910431a01ef524bf8cc2ef1f96ccd2d6cba7",
        "out": "0ac854485f07617cf6d8c370a295681854025bec",
        "edits": [
            (0x218E780, BX_LR),
            (0x218E908, BX_LR),
            (0xB2159FE, bytes.fromhex("002100bf002200bf")),
        ],
    },
}


def sha1(b):
    return hashlib.sha1(b).hexdigest()


def main(kind, src, dst):
    p = PATCHES[kind]
    d = bytearray(open(src, "rb").read())
    if sha1(d) != p["in"]:
        sys.exit("%s: SHA-1 %s, expected the unmodified 11D257 iPod5,1 file (%s)" % (src, sha1(d), p["in"]))
    for off, new in p["edits"]:
        d[off:off + len(new)] = new
    if sha1(d) != p["out"]:
        sys.exit("patched result has an unexpected SHA-1 %s" % sha1(d))
    open(dst, "wb").write(d)
    print("%s: %d patches applied, sha1 %s" % (kind, len(p["edits"]), p["out"]))


if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in PATCHES:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
