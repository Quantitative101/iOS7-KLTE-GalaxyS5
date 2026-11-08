"""Write a minimal Apple-format (XNU) device tree for QEMU bring-up.

Format: node = u32 nprops, u32 nchildren, nprops * (char name[32], u32 length, data padded to 4), then children.
Usage: python3 -I tools/make-dt.py out.dtb
"""
import struct
import sys


def prop(name, value):
    if isinstance(value, str):
        value = value.encode() + b"\0"
    pad = (-len(value)) % 4
    return name.encode().ljust(32, b"\0") + struct.pack("<I", len(value)) + value + b"\0" * pad


def node(props, children=()):
    out = struct.pack("<II", len(props), len(children))
    out += b"".join(prop(k, v) for k, v in props)
    out += b"".join(children)
    return out


chosen = node([("name", "chosen"), ("firmware-version", "uniLoader")])
cpus = node([("name", "cpus")], [node([("name", "cpu0"), ("device_type", "cpu")])])
memory = node([("name", "memory-map")])
options = node([("name", "options")])
arm_io = node([("name", "arm-io"), ("device_type", "arm-io"), ("compatible", "arm-io\0")])
root = node(
    [("name", "device-tree"), ("compatible", "AppleARM\0"), ("model", "QEMU-virt"), ("device_type", "bootrom")],
    [chosen, cpus, memory, arm_io, options],
)
open(sys.argv[1], "wb").write(root)
print("wrote %d bytes" % len(root))
