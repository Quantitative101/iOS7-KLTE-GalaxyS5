"""Disassemble a window of Thumb code from an iOS 7 armv7 dyld shared cache.

Usage: python3 -I tools/dsc-disasm.py dyld_shared_cache_armv7 START_VA LENGTH out_prefix
Writes out_prefix.s (Thumb .byte dump) and assembles/dumps it with Xcode's clang + llvm-objdump; addresses printed in
the listing are real shared-cache VAs (the object is linked at START_VA).
"""
import struct
import subprocess
import sys


def main(path, start, length, prefix):
    start = int(start, 0) & ~1
    length = int(length, 0)
    f = open(path, "rb")
    hdr = f.read(0x60)
    mapoff, mapcnt = struct.unpack_from("<2I", hdr, 16)
    f.seek(mapoff)
    maps = [struct.unpack("<QQQII", f.read(32)) for _ in range(mapcnt)]
    for a, sz, fo, _mp, _ip in maps:
        if a <= start < a + sz:
            f.seek(fo + start - a)
            data = f.read(length)
            break
    else:
        sys.exit("address not mapped")
    with open(prefix + ".s", "w") as s:
        s.write(".thumb\n.syntax unified\n.text\n.globl _start\n.thumb_func\n_start:\n")
        for i in range(0, len(data), 16):
            s.write(".byte " + ",".join("0x%02x" % b for b in data[i:i + 16]) + "\n")
    subprocess.check_call(["xcrun", "clang", "-c", "-arch", "armv7", "--target=armv7-apple-darwin", "-o", prefix + ".o", prefix + ".s"])
    out = subprocess.check_output(
        ["xcrun", "llvm-objdump", "-d", "--triple=thumbv7-apple-darwin", "--no-show-raw-insn","--start-address=0", prefix + ".o"]).decode()
    for line in out.splitlines():
        parts = line.split(":", 1)
        if len(parts) == 2 and parts[0].strip().isalnum():
            try:
                print("%08x:%s" % (start + int(parts[0], 16), parts[1]))
                continue
            except ValueError:
                pass
        print(line)


if __name__ == "__main__":
    main(*sys.argv[1:5])
