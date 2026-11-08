"""Add an LC_LOAD_DYLIB command to the armv7 slice of a (possibly fat) Mach-O, using the header padding.

Usage: python3 -I tools/inject-dylib.py IN OUT /usr/lib/j1shim.dylib
The first section/segment data offset limits the space; a code signature is ignored (the QEMU kernel does not enforce it).
"""
import struct
import sys

LC_LOAD_DYLIB = 0xC
LC_SEGMENT = 0x1
FAT_MAGIC = 0xCAFEBABE
MH_MAGIC = 0xFEEDFACE
CPU_TYPE_ARM = 12


def patch_slice(d, off, dylib):
    magic, cputype, _sub, _ft, ncmds, sizeofcmds, _flags = struct.unpack_from("<7I", d, off)
    assert magic == MH_MAGIC, hex(magic)
    base = off + 28
    # smallest file offset of any section data = end of free header space
    limit = None
    pos = base
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", d, pos)
        if cmd == LC_SEGMENT:
            nsects = struct.unpack_from("<I", d, pos + 48)[0]
            for s in range(nsects):
                sec = pos + 56 + s * 68
                soff = struct.unpack_from("<I", d, sec + 40)[0]
                if soff and (limit is None or soff < limit):
                    limit = soff
        pos += size
    name = dylib.encode() + b"\0"
    cmdsize = (24 + len(name) + 3) & ~3
    end_cmds = base + sizeofcmds
    if limit is None or off + limit - end_cmds < cmdsize:
        raise SystemExit("no room for a load command (%d free)" % (off + (limit or 0) - end_cmds))
    blob = struct.pack("<IIIIII", LC_LOAD_DYLIB, cmdsize, 24, 2, 0x10000, 0x10000) + name
    blob += b"\0" * (cmdsize - len(blob))
    d[end_cmds:end_cmds + cmdsize] = blob
    struct.pack_into("<II", d, off + 16, ncmds + 1, sizeofcmds + cmdsize)


def main(inp, outp, dylib):
    d = bytearray(open(inp, "rb").read())
    magic = struct.unpack_from(">I", d, 0)[0]
    if magic == FAT_MAGIC:
        n = struct.unpack_from(">I", d, 4)[0]
        for i in range(n):
            cputype, _sub, offset, _size, _al = struct.unpack_from(">5I", d, 8 + i * 20)
            if cputype == CPU_TYPE_ARM:
                patch_slice(d, offset, dylib)
    else:
        patch_slice(d, 0, dylib)
    open(outp, "wb").write(d)
    print("patched", outp)


if __name__ == "__main__":
    main(*sys.argv[1:4])
