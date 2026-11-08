#!/bin/bash
# Build the S5 root filesystem from the iPod touch 5 (iPod5,1) iOS 7.1.2 11D257 root, applying the same changes the
# iPad2,2 root carries on the phone (run 133+). Local files only; nothing here is published.
#   bash s5/tools/build-ipod-root.sh
# Inputs:
#   ports/ipsw/ipod5_1_7.1.2/ex/rootfs.dmg               decrypted iPod root (ports/tools/vfdecrypt.py)
#   ports/ipsw/ipod5_1_7.1.2/ex/dyld_shared_cache_armv7.patched   iPod cache with the ported patches
#   CUR (default: the mounted current S5 root image, /Volumes/Sochi11D257.K94OS): source of our patched SpringBoard,
#     backboardd, fairplayd.A1 (fbtest) + plist, j1shim.dylib and the list of LaunchDaemons to keep
# Output: ports/ipsw/ipod5_1_7.1.2/ex/root-ipod.hfs, a bare HFSX volume of exactly 3670016 sectors (1792 MiB)
set -euo pipefail
P=$(cd "$(dirname "$0")/../.." && pwd)
E=$P/ports/ipsw/ipod5_1_7.1.2/ex
CUR=${CUR:-/Volumes/Sochi11D257.K94OS}
SECTORS=3670016
[ -f "$CUR/usr/lib/j1shim.dylib" ] || { echo "current S5 root not mounted at $CUR" >&2; exit 1; }
[ -f "$E/dyld_shared_cache_armv7.patched" ] || { echo "missing patched iPod cache" >&2; exit 1; }
rm -f "$E/root_rw.dmg" "$E/root_ipod.cdr"
hdiutil convert "$E/rootfs.dmg" -format UDRW -o "$E/root_rw" > /dev/null
hdiutil resize -size 1790m "$E/root_rw.dmg" > /dev/null
DEV=$(hdiutil attach "$E/root_rw.dmg" -nobrowse -noverify -owners off | awk '/Apple_HFS/ {print $1}')
M=$(mount | awk -v d="$DEV" '$1 == d {for (i = 3; i <= NF; i++) if ($i ~ /^\(/) break; else s = s (s ? " " : "") $i; print s}')
echo "mounted $DEV at $M"
# 1. space and noise: ringtones and the new UI sounds (as on the iPad root)
rm -rf "$M/Library/Ringtones" "$M/System/Library/Audio/UISounds/New"
# 2. LaunchDaemons: keep the same set the phone runs today (iPad K94 jetsam properties -> iPod N78), park the rest
mkdir -p "$M/System/Library/LaunchDaemons.disabled"
KEEP=$(ls "$CUR/System/Library/LaunchDaemons" | sed 's/jetsamproperties\.K94/jetsamproperties.N78/')
for f in "$M"/System/Library/LaunchDaemons/*; do
    b=$(basename "$f")
    echo "$KEEP" | grep -qxF "$b" || mv "$f" "$M/System/Library/LaunchDaemons.disabled/"
done
# 3. our files (written with cat so existing files keep their root ownership)
cat "$CUR/System/Library/CoreServices/SpringBoard.app/SpringBoard" > "$M/System/Library/CoreServices/SpringBoard.app/SpringBoard"
cat "$CUR/usr/libexec/backboardd" > "$M/usr/libexec/backboardd"
cat "$CUR/usr/sbin/fairplayd.A1" > "$M/usr/sbin/fairplayd.A1"
# launchd only loads root-owned plists, and files created on this owners-off mount belong to us: rename a parked
# root-owned plist into place and overwrite its contents (run 133: a cp'd plist never loaded, so no GL payload)
mv "$M/System/Library/LaunchDaemons.disabled/com.apple.ABDatabaseDoctor.plist" "$M/System/Library/LaunchDaemons/com.apple.fairplayd.A1.plist"
cat "$CUR/System/Library/LaunchDaemons/com.apple.fairplayd.A1.plist" > "$M/System/Library/LaunchDaemons/com.apple.fairplayd.A1.plist"
cp "$CUR/usr/lib/j1shim.dylib" "$M/usr/lib/j1shim.dylib"
chmod 755 "$M/usr/lib/j1shim.dylib" "$M/usr/sbin/fairplayd.A1"
cat "$E/dyld_shared_cache_armv7.patched" > "$M/System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7"
ls "$M/System/Library/LaunchDaemons" | wc -l | sed 's/^/launch daemons kept: /'
df -h "$M" | tail -1
sync
hdiutil detach "$DEV" > /dev/null
hdiutil convert "$E/root_rw.dmg" -format UDTO -o "$E/root_ipod" > /dev/null
python3 -I "$P/ports/tools/extract-hfs.py" "$E/root_ipod.cdr" "$E/root-ipod.hfs"
SZ=$(wc -c < "$E/root-ipod.hfs" | tr -d ' ')
echo "volume bytes $SZ (partition holds $((SECTORS * 512)))"
[ "$SZ" -le $((SECTORS * 512)) ] || { echo "volume larger than the partition" >&2; exit 1; }
rm -f "$E/root_rw.dmg" "$E/root_ipod.cdr"
shasum "$E/root-ipod.hfs"
