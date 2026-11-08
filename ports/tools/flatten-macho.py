"""Flatten a 32-bit little-endian Mach-O kernel into a memory image for uniLoader.

uniLoader copies the Image blob straight to PHYS_BASE+1 page and treats VA = VIRT_BASE + offset, so the file layout must
equal the VM layout. The linker packs zerofill (__common/__bss) out of the file and puts __LINKEDIT in its place, so a raw
copy lands symbol-table bytes where the kernel expects zeroed BSS. This places each segment's file bytes at
(vmaddr - lowest vmaddr + page), zero-fills the rest, and drops __LINKEDIT.
Usage: python3 -I tools/flatten-macho.py mach_kernel out.img
"""
import struct
import sys

LC_SEGMENT = 0x1
PAGE = 0x1000


def main(src, dst):
    data = open(src, "rb").read()
    magic, _cpu, _sub, _ftype, ncmds, _sizecmds, _flags = struct.unpack_from("<7I", data, 0)
    assert magic == 0xFEEDFACE, "not a 32-bit little-endian Mach-O"
    off = 28
    segs = []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<2I", data, off)
        if cmd == LC_SEGMENT:
            name = data[off + 8:off + 24].split(b"\0")[0].decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<4I", data, off + 24)
            segs.append((name, vmaddr, vmsize, fileoff, filesize))
        off += size
    segs = [s for s in segs if s[0] != "__LINKEDIT" and s[2] > 0]
    base = min(s[1] for s in segs)
    top = max(s[1] + s[2] for s in segs)
    img = bytearray(top - base + PAGE)
    # Keep the Mach-O header at the start of the image (uniLoader parses it for the entry point).
    hdr = min(s[3] for s in segs if s[4] > 0)
    first = min(s[1] for s in segs if s[3] == 0)
    for name, vmaddr, vmsize, fileoff, filesize in segs:
        if filesize:
            dst_off = vmaddr - base
            img[dst_off:dst_off + filesize] = data[fileoff:fileoff + filesize]
    open(dst, "wb").write(bytes(img[:top - base]))
    print("flattened %d segments, vm 0x%x..0x%x -> %d bytes" % (len(segs), base, top, top - base))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
