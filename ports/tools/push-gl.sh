#!/bin/sh
# Rebuild the shim dylibs and push j1gl.dylib into disk.img's payload sectors (see tools/gl-push.py). Works for the next boot
# and, while QEMU is running, as a live reload: the guest daemon notices the new sequence number and restarts backboardd.
# Usage: sh tools/push-gl.sh
P=$(cd "$(dirname "$0")/.." && pwd)
set -e
sh "$P/glshim/build.sh" > /dev/null 2>&1
python3 -I "$P/tools/gl-push.py" "$P/build/disk.img" "$P/glshim/j1gl.dylib"
