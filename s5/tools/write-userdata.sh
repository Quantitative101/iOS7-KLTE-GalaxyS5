#!/bin/bash
# Write a disk image onto the S5 userdata partition (/dev/block/mmcblk0p26) from TWRP 3.7, in 128 MiB pieces:
# TWRP's old adbd cannot push to a block device (it replaces the node with a file in the RAM /dev) and has no exec-out,
# so each piece goes to /tmp, is dd'd at its offset and checked there.
#   bash s5/tools/write-userdata.sh IMAGE [FIRST_PIECE]
set -eu
IMG=$1
FIRST=${2:-0}
PIECE=$((128 * 1024 * 1024))
SIZE=$(wc -c < "$IMG" | tr -d " ")
N=$(((SIZE + PIECE - 1) / PIECE))
TMP=$(mktemp -d)
# TWRP mounts userdata at /data (and /sdcard on it): unmount before writing under it
adb shell "umount /sdcard 2>/dev/null; umount /data 2>/dev/null; mount" | grep -q mmcblk0p26 && { echo "userdata still mounted" >&2; exit 1; }
trap 'rm -rf "$TMP"' EXIT
for ((i = FIRST; i < N; i++)); do
    if [ -n "${ONLY:-}" ] && ! echo " $ONLY " | grep -q " $i "; then continue; fi
    dd if="$IMG" of="$TMP/p" bs=$PIECE skip=$i count=1 2>/dev/null
    LEN=$(wc -c < "$TMP/p" | tr -d " ")
    WANT=$(shasum -a 256 "$TMP/p" | cut -d' ' -f1)
    adb push "$TMP/p" /tmp/p >/dev/null 2>&1
    # busybox dd in TWRP 2.8.5 treats seek= like skip= (it skips input), so write through a loop device that starts at
    # the piece's offset into userdata; reading back with skip= is fine.
    GOT=$(adb shell "losetup -o $((i * PIECE)) /dev/block/loop7 /dev/block/mmcblk0p26 && \
        dd if=/tmp/p of=/dev/block/loop7 bs=1048576 2>/dev/null; sync; losetup -d /dev/block/loop7; \
        dd if=/dev/block/mmcblk0p26 bs=1048576 skip=$((i * 128)) count=$((LEN / 1048576)) 2>/dev/null | sha256sum; rm /tmp/p" |
        cut -d' ' -f1)
    if [ $((LEN % 1048576)) -ne 0 ]; then
        echo "piece $i: length $LEN is not whole MiB, check by hand" >&2
    fi
    if [ "$GOT" != "$WANT" ]; then
        echo "piece $i/$N MISMATCH (device $GOT, image $WANT): stopping; rerun with FIRST_PIECE=$i" >&2
        exit 1
    fi
    echo "piece $i/$N ok ($(adb shell cat /sys/class/power_supply/battery/capacity | tr -d '\r')%)"
done
echo "all $N pieces written and verified"
