#!/bin/sh
# Shim-only fast path: rebuild glshim/j1shim.dylib and drop it into the existing patched root image, then re-wrap disk.img.
# No kernel build, no hdiutil convert / extract-hfs (those are what tools/patch-root.sh does). Needs a root.hfs produced by a
# previous full `tools/patch-root.sh` run (with QEMU_BB_INJECT=1 so backboardd already loads /usr/lib/j1shim.dylib).
# Ownership caveat (see patch-root.sh): launchd rejects non-root-owned plists, and files created on the mounted volume do not
# get root ownership, so an existing file is rewritten in place (cat >) to keep its owner. Only /usr/lib/j1shim.dylib is touched.
# Usage: sh tools/fast-shim.sh
P=$(cd "$(dirname "$0")/.." && pwd)
D=$P/ipsw/ipad2_2_7.1.2/11D257__iPad2,2
M=$P/ipsw/ipad2_2_7.1.2/fastmnt
T0=$(date +%s)
set -e
sh "$P/glshim/build.sh" > /dev/null
T1=$(date +%s)
[ -f "$D/root.hfs" ] || { echo "no $D/root.hfs: run a full tools/patch-root.sh first" >&2; exit 1; }
mkdir -p "$M"
hdiutil attach "$D/root.hfs" -imagekey diskimage-class=CRawDiskImage -nobrowse -noverify -owners off -mountpoint "$M" > /dev/null
trap 'hdiutil detach "$M" > /dev/null 2>&1 || true' EXIT
if [ -f "$M/usr/lib/j1shim.dylib" ]; then
  chmod u+w "$M/usr/lib/j1shim.dylib" || true
  cat "$P/glshim/j1shim.dylib" > "$M/usr/lib/j1shim.dylib"
else
  cp "$P/glshim/j1shim.dylib" "$M/usr/lib/j1shim.dylib"
  chmod 755 "$M/usr/lib/j1shim.dylib"
fi
sync
hdiutil detach "$M" > /dev/null
trap - EXIT
T2=$(date +%s)
python3 -I "$P/tools/make-disk.py" "$D/root.hfs" "$D/var.hfs" "$D/disk.img" > /dev/null
T3=$(date +%s)
echo "fast-shim: shim build $((T1 - T0)) s, mount+write+detach $((T2 - T1)) s, make-disk $((T3 - T2)) s, total $((T3 - T0)) s"
