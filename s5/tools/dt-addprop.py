"""dt-addprop.py DTB NODE NAME=U32 [NAME=U32 ...] - add (or replace) 32-bit properties on one top-level node of an
Apple-format (XNU) flattened device tree, in place.

Format: node = u32 nprops, u32 nchildren, nprops * (char name[32], u32 length, data padded to 4), then the children.
NODE is the value of the child's "name" property (e.g. chosen). Used by s5/stub/build.sh: MobileGestalt reads
"display-scale" and "display-rotation" from IODeviceTree:/chosen, and without them UIKit's screen scale is 0
(S5 run 88), which made SpringBoard abort (NaN layer positions, nil UIKit Dynamics bodies).
"""
import struct
import sys


def parse_node(b, o):
    nprops, nchild = struct.unpack_from("<II", b, o)
    p = o + 8
    props = []
    for _ in range(nprops):
        name = b[p:p + 32].split(b"\0")[0].decode()
        ln = struct.unpack_from("<I", b, p + 32)[0]
        # the top bit of the length can flag a placeholder value; keep the raw word
        dl = ln & 0x7fffffff
        props.append((name, ln, b[p + 36:p + 36 + dl]))
        p += 36 + dl + ((-dl) % 4)
    children = []
    for _ in range(nchild):
        child, p = parse_node(b, p)
        children.append(child)
    return (props, children), p


def emit(node):
    props, children = node
    out = struct.pack("<II", len(props), len(children))
    for name, ln, data in props:
        out += name.encode().ljust(32, b"\0") + struct.pack("<I", ln) + data + b"\0" * ((-len(data)) % 4)
    for c in children:
        out += emit(c)
    return out


def main():
    path, want = sys.argv[1], sys.argv[2]
    adds = [(kv.split("=")[0], int(kv.split("=")[1], 0)) for kv in sys.argv[3:]]
    b = open(path, "rb").read()
    root, end = parse_node(b, 0)
    assert end == len(b), "trailing bytes after the root node (%d of %d)" % (end, len(b))
    hit = None
    for c in root[1]:
        if any(n == "name" and d.split(b"\0")[0].decode() == want for n, _, d in c[0]):
            hit = c
    if hit is None:
        sys.exit("node %s not found" % want)
    props = [p for p in hit[0] if p[0] not in {k for k, _ in adds}]
    props += [(k, 4, struct.pack("<I", v)) for k, v in adds]
    hit[0][:] = props
    out = emit(root)
    open(path, "wb").write(out)
    print("%s: /%s += %s (%d -> %d bytes)" % (path, want, ", ".join("%s=%d" % a for a in adds), len(b), len(out)))


if __name__ == "__main__":
    main()
