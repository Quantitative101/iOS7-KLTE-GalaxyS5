#!/bin/bash
# Write the sector runs in which NEW differs from OLD (equal-size volume images) to the S5 userdata partition at
# BASE_SECTOR (the volume's start), verifying each run on the device.
#   bash s5/tools/write-sector-runs.sh OLD NEW BASE_SECTOR RUNS_FILE
# RUNS_FILE: lines "start count" (sectors within the volume), e.g. from secdiff.py.
set -euo pipefail
OLD=$1 NEW=$2 BASE=$3 RUNS=$4
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
adb shell "umount /sdcard 2>/dev/null; umount /data 2>/dev/null; true" </dev/null
while read -r START COUNT; do
    [[ "$START" =~ ^[0-9]+$ ]] || continue
    dd if="$NEW" of="$TMP/r" bs=512 skip="$START" count="$COUNT" 2>/dev/null
    WANT=$(shasum "$TMP/r" | cut -c1-40)
    adb push "$TMP/r" /tmp/r </dev/null >/dev/null 2>&1
    GOT=$(adb shell "L=\$(losetup -f); losetup -o $(((BASE + START) * 512)) \$L /dev/block/mmcblk0p26 && \
        dd if=/tmp/r of=\$L bs=512 conv=fsync 2>/dev/null; sync; echo 3 > /proc/sys/vm/drop_caches; \
        dd if=\$L bs=512 count=$COUNT 2>/dev/null | sha1sum; losetup -d \$L; rm /tmp/r" </dev/null | cut -c1-40)
    if [ "$GOT" != "$WANT" ]; then
        echo "run $START+$COUNT MISMATCH ($GOT vs $WANT)" >&2
        exit 1
    fi
    echo "run $START+$COUNT ok"
done < "$RUNS"
echo "all runs written and verified"
