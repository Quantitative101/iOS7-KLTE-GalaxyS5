#!/bin/bash
# Build the S5 root filesystem from the decrypted iPod touch 5 (iPod5,1) iOS 7.1.2 11D257 root. Output stays local:
# it is Apple's software and must not be redistributed.
#   bash s5/tools/build-ipod-root.sh [ROOTFS_DMG]
# Inputs:
#   ROOTFS_DMG (default ports/ipsw/ipod5_1_7.1.2/rootfs.dmg): the decrypted root filesystem (docs/BUILDING.md, step 4)
#   ports/glshim/j1shim.dylib (sh ports/glshim/build.sh) and ports/testapp/fbtest (sh ports/testapp/build.sh)
# Output: ports/ipsw/ipod5_1_7.1.2/root-ipod.hfs, a bare HFSX volume of at most 3670016 sectors (1792 MiB)
#
# Changes made to the stock root:
#   1. ringtones and the iOS 7 "New" UI sounds removed (space)
#   2. LaunchDaemons cut to the 48 jobs in s5/root/launchdaemons.keep; the rest moved to LaunchDaemons.disabled
#   3. SpringBoard and the dyld shared cache byte-patched (s5/tools/patch-ipod-binaries.py)
#   4. backboardd gets LC_LOAD_DYLIB /usr/lib/j1shim.dylib (ports/tools/inject-dylib.py); j1shim.dylib installed
#   5. /usr/sbin/fairplayd.O2 replaced by fbtest (payload reload daemon), its launchd job set to RunAtLoad + KeepAlive
#   6. DefaultWallpapers.plist: a default wallpaper for ProductType "QEMU-virt" (the device tree's model name)
#   7. @1x copies of the @2x-only artwork (s5/tools/make-1x-images.py), because the S5 runs a 480x800 @1x screen
# Ownership: on this owners-off mount every file we create belongs to uid 501 (mobile on iOS), and launchd only loads
# root-owned job plists and binaries. So existing files are overwritten in place with cat, which keeps their owner.
set -euo pipefail
P=$(cd "$(dirname "$0")/../.." && pwd)
E=$P/ports/ipsw/ipod5_1_7.1.2
SRC=${1:-$E/rootfs.dmg}
OUT=$E/root-ipod.hfs
SECTORS=3670016
SHIM=$P/ports/glshim/j1shim.dylib
FB=$P/ports/testapp/fbtest
[ -f "$SRC" ] || { echo "no decrypted root filesystem at $SRC" >&2; exit 1; }
[ -s "$SHIM" ] || { echo "missing $SHIM: sh ports/glshim/build.sh" >&2; exit 1; }
[ -s "$FB" ] || { echo "missing $FB: sh ports/testapp/build.sh" >&2; exit 1; }
python3 -c 'import PIL' 2>/dev/null || { echo "make-1x-images.py needs Pillow: python3 -m pip install Pillow" >&2; exit 1; }
W=$(mktemp -d "$E/work.XXXXXX")
DEV=
cleanup() {
    [ -n "$DEV" ] && hdiutil detach "$DEV" > /dev/null 2>&1 || true
    rm -rf "$W"
}
trap cleanup EXIT
echo "converting $SRC"
hdiutil convert "$SRC" -format UDRW -o "$W/root_rw" > /dev/null
hdiutil resize -size 1790m "$W/root_rw.dmg" > /dev/null
DEV=$(hdiutil attach "$W/root_rw.dmg" -nobrowse -noverify -owners off -mountpoint "$W/mnt" | awk '/Apple_HFS/ {print $1}')
M=$W/mnt
[ -f "$M/System/Library/CoreServices/SpringBoard.app/SpringBoard" ] || { echo "not an iOS root at $M" >&2; exit 1; }
echo "mounted $DEV at $M"

# 1. space
rm -rf "$M/Library/Ringtones" "$M/System/Library/Audio/UISounds/New"

# 2. launchd jobs
mkdir -p "$M/System/Library/LaunchDaemons.disabled"
KEEP=$P/s5/root/launchdaemons.keep
for f in "$M"/System/Library/LaunchDaemons/*; do
    b=$(basename "$f")
    grep -qxF "$b" "$KEEP" || mv "$f" "$M/System/Library/LaunchDaemons.disabled/"
done
while read -r b; do
    [ -e "$M/System/Library/LaunchDaemons/$b" ] || { echo "kept job $b is missing from this root" >&2; exit 1; }
done < "$KEEP"
echo "launch daemons kept: $(ls "$M/System/Library/LaunchDaemons" | wc -l | tr -d ' ')"

# 3. byte patches
SB=$M/System/Library/CoreServices/SpringBoard.app/SpringBoard
DSC=$M/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7
python3 -I "$P/s5/tools/patch-ipod-binaries.py" springboard "$SB" "$W/SpringBoard"
python3 -I "$P/s5/tools/patch-ipod-binaries.py" dyld-cache "$DSC" "$W/dsc"
chmod u+w "$SB" "$DSC" 2>/dev/null || true
cat "$W/SpringBoard" > "$SB"
cat "$W/dsc" > "$DSC"
rm -f "$W/dsc"

# 4. renderer loader
BB=$M/usr/libexec/backboardd
python3 -I "$P/ports/tools/inject-dylib.py" "$BB" "$W/backboardd" /usr/lib/j1shim.dylib
chmod u+w "$BB" 2>/dev/null || true
cat "$W/backboardd" > "$BB"
cp "$SHIM" "$M/usr/lib/j1shim.dylib"
chmod 755 "$M/usr/lib/j1shim.dylib"

# 5. payload reload daemon in place of fairplayd.O2
FP=$M/usr/sbin/fairplayd.O2
chmod u+w "$FP"
cat "$FB" > "$FP"
chmod 555 "$FP"
JOB=$M/System/Library/LaunchDaemons/com.apple.fairplayd.O2.plist
chmod u+w "$JOB" 2>/dev/null || true
cat > "$JOB" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.apple.fairplayd.O2</string>
<key>ProgramArguments</key><array><string>/usr/sbin/fairplayd.O2</string></array>
<key>RunAtLoad</key><true/>
<key>KeepAlive</key><true/>
</dict></plist>
PL

# 6. default wallpaper for our ProductType (MobileGestalt reports the device tree model, "QEMU-virt")
WP=$M/Library/Wallpaper/DefaultWallpapers.plist
cp "$WP" "$W/wp.plist"
plutil -insert QEMU-virt -json \
    '{"unknown":{"unknown":{"default":{"path":"/Library/Wallpaper/iPod/101@2x~ipod.png","type":"static"}}}}' "$W/wp.plist"
chmod u+w "$WP" 2>/dev/null || true
cat "$W/wp.plist" > "$WP"

# 7. @1x artwork (plain python3, not -I: Pillow is usually a user-site install)
python3 "$P/s5/tools/make-1x-images.py" "$M" "$W/1x" Applications System/Library/CoreServices \
    System/Library/Frameworks System/Library/PrivateFrameworks System/Library/PreferenceBundles \
    System/Library/AccessibilityBundles
ditto "$W/1x" "$M"

df -h "$M" | tail -1
sync
hdiutil detach "$DEV" > /dev/null
DEV=
hdiutil convert "$W/root_rw.dmg" -format UDTO -o "$W/root" > /dev/null
python3 -I "$P/ports/tools/extract-hfs.py" "$W/root.cdr" "$OUT"
SZ=$(wc -c < "$OUT" | tr -d ' ')
echo "volume bytes $SZ (partition holds $((SECTORS * 512)))"
[ "$SZ" -le $((SECTORS * 512)) ] || { echo "volume larger than the partition" >&2; exit 1; }
shasum "$OUT"
