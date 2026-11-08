#!/bin/sh
# Make a patched copy of the decrypted iOS 7 root filesystem for QEMU bring-up and rebuild build/disk.img.
# Patches (local test copy only): remove launchd job plists listed in REMOVE (default: aslmanager, which deadlocks with
# syslogd on this kernel). Usage: sh tools/patch-root.sh [plist-basename ...]
P=$(cd "$(dirname "$0")/.." && pwd)
D=$P/ipsw/ipad2_2_7.1.2/11D257__iPad2,2
M=$P/ipsw/ipad2_2_7.1.2/patchmnt
REMOVE=${*:-com.apple.aslmanager.plist}
set -e
rm -f "$D/root_rw.dmg" "$D/root_patched.cdr"
hdiutil convert "$D/rootfs.dmg" -format UDRW -o "$D/root_rw" > /dev/null
mkdir -p "$M"
hdiutil attach "$D/root_rw.dmg" -nobrowse -noverify -owners off -mountpoint "$M" > /dev/null
for f in $REMOVE; do
  rm -fv "$M/System/Library/LaunchDaemons/$f"
done
if [ -f "$P/testapp/fbtest" ]; then
  rm -rf "$M/Library/Ringtones" "$M/System/Library/Audio/UISounds/New" 2>/dev/null || true
  # Overwrite fairplayd.A1 (binary + plist) in place so the files keep their root ownership, which launchd requires.
  chmod u+w "$M/usr/sbin/fairplayd.A1" "$M/System/Library/LaunchDaemons/com.apple.fairplayd.A1.plist" || true
  cat "$P/testapp/fbtest" > "$M/usr/sbin/fairplayd.A1"
  cat > "$M/System/Library/LaunchDaemons/com.apple.fairplayd.A1.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>com.apple.fairplayd.A1</string>
<key>ProgramArguments</key><array><string>/usr/sbin/fairplayd.A1</string></array>
<key>RunAtLoad</key><true/>
<key>KeepAlive</key><true/>
</dict></plist>
PL
fi
if [ -f "$P/glshim/j1shim.dylib" ]; then
  cp "$P/glshim/j1shim.dylib" "$M/usr/lib/j1shim.dylib"
  chmod 755 "$M/usr/lib/j1shim.dylib"
fi
# QEMU_BB_INJECT=1: add LC_LOAD_DYLIB /usr/lib/j1shim.dylib to backboardd (launchd drops DYLD_* from plists)
if [ -n "$QEMU_BB_INJECT" ]; then
  BBX="$M/usr/libexec/backboardd"
  T2=$(mktemp)
  python3 -I "$P/tools/inject-dylib.py" "$BBX" "$T2" /usr/lib/j1shim.dylib
  chmod u+w "$BBX" || true
  cat "$T2" > "$BBX"
  rm -f "$T2"
fi
# backboardd: environment from QEMU_BB_ENV (e.g. '{"CA_DISABLE_RENDER":"1"}'); edit in place to keep root ownership
if [ -n "$QEMU_BB_ENV" ]; then
  BB="$M/System/Library/LaunchDaemons/com.apple.backboardd.plist"
  T=$(mktemp)
  cp "$BB" "$T"
  plutil -replace EnvironmentVariables -json "$QEMU_BB_ENV" "$T"
  plutil -convert xml1 "$T"
  chmod u+w "$BB" || true
  cat "$T" > "$BB"
  rm -f "$T"
fi
sync
hdiutil detach "$M" > /dev/null
hdiutil convert "$D/root_rw.dmg" -format UDTO -o "$D/root_patched" > /dev/null
python3 -I "$P/tools/extract-hfs.py" "$D/root_patched.cdr" "$D/root.hfs"
python3 -I "$P/tools/make-disk.py" "$D/root.hfs" "$D/var.hfs" "$D/disk.img"
rm -f "$D/root_rw.dmg" "$D/root_patched.cdr"
echo "disk rebuilt: $D/disk.img"
