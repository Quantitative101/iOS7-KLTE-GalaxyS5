#!/bin/bash
# Build the /private/var volume (MBR partition 2, mounted by /etc/fstab as /dev/disk0s2) from the decrypted root
# filesystem's own /private/var. Output stays local (Apple files).
#   bash s5/tools/build-var.sh [ROOTFS_DMG]
# Output: ports/ipsw/ipod5_1_7.1.2/var.hfs, a bare 640 MiB HFS+ volume (1310720 sectors)
# The copy is made by your user on an owners-off mount, so files end up owned by uid 501 (mobile on iOS); this is how
# the volume that ran on the phone was made. com.apple.migration.plist (LastSystemVersion 11D257) in /var/root and
# /var/mobile stops backboardd from waiting for a data migration (DMPerformMigrationIfNeeded) on first boot.
set -euo pipefail
P=$(cd "$(dirname "$0")/../.." && pwd)
E=$P/ports/ipsw/ipod5_1_7.1.2
SRC=${1:-$E/rootfs.dmg}
OUT=$E/var.hfs
[ -f "$SRC" ] || { echo "no decrypted root filesystem at $SRC" >&2; exit 1; }
W=$(mktemp -d "$E/work.XXXXXX")
RDEV=
VDEV=
cleanup() {
    [ -n "$RDEV" ] && hdiutil detach "$RDEV" > /dev/null 2>&1 || true
    [ -n "$VDEV" ] && hdiutil detach "$VDEV" > /dev/null 2>&1 || true
    rm -rf "$W"
}
trap cleanup EXIT
RDEV=$(hdiutil attach "$SRC" -readonly -nobrowse -noverify -noautofsck -mountpoint "$W/root" | awk '/Apple_HFS/ {print $1}')
[ -d "$W/root/private/var/mobile" ] || { echo "no private/var in $SRC" >&2; exit 1; }
hdiutil create -size 640m -fs HFS+ -layout NONE -type UDIF -volname var "$W/var_img" > /dev/null
VDEV=$(hdiutil attach "$W/var_img.dmg" -nobrowse -noverify -owners off -mountpoint "$W/var" | awk 'NR == 1 {print $1}')
ditto "$W/root/private/var" "$W/var"
chmod 1777 "$W/var/tmp" # ditto drops the sticky bit on this mount
for d in root mobile; do
    mkdir -p "$W/var/$d/Library/Preferences"
    plutil -create xml1 "$W/var/$d/Library/Preferences/com.apple.migration.plist"
    plutil -insert LastSystemVersion -string 11D257 "$W/var/$d/Library/Preferences/com.apple.migration.plist"
done
df -h "$W/var" | tail -1
sync
hdiutil detach "$VDEV" > /dev/null
VDEV=
# -layout NONE: the image is the bare volume; keep exactly 640 MiB
head -c $((640 * 1024 * 1024)) "$W/var_img.dmg" > "$OUT"
[ "$(wc -c < "$OUT" | tr -d ' ')" -eq $((1310720 * 512)) ]
[ "$(xxd -s 1024 -l 2 -p "$OUT")" = 482b ] || { echo "no HFS+ signature in $OUT" >&2; exit 1; }
shasum "$OUT"
