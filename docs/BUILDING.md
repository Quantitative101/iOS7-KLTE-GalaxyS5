# Building and flashing the Galaxy S5 port

This guide goes from a fresh clone to a phone booting iOS 7.1.2. Every command is run from the repository root on a Mac
unless it says *on the phone* (an `adb shell` inside TWRP).

> **Read this first.** This is an experimental research port. Following this guide erases all Android data on the
> phone's `userdata` partition and replaces the `boot` partition. You need TWRP and must be able to restore the phone
> yourself. Nothing here touches the bootloader, modem, EFS or any other partition, but back them up anyway.

The repository contains no Apple or Samsung files. You download the iOS firmware from Apple yourself and build the
iOS volumes on your own Mac; the scripts never upload or publish anything.

## What you build

| Output | Built from | Goes to |
|---|---|---|
| `s5/build/xnu-boot.img` | the kernel (`xnu/` patch), the stub (`s5/stub/`), your own stock boot image | `boot` partition |
| `ports/ipsw/ipod5_1_7.1.2/root-ipod.hfs` | the iPod5,1 iOS 7.1.2 root filesystem plus the port's patches | `userdata`, MBR partition 1 |
| `ports/ipsw/ipod5_1_7.1.2/var.hfs` | the same root filesystem's `/private/var` | `userdata`, MBR partition 2 |
| renderer payload | `ports/glshim/j1gl.dylib` | `userdata`, MBR partition 3 |
| `s5/build/userdata.img` | the three above, behind an MBR | written to the start of `userdata` |

The kernel finds `userdata` at fixed eMMC sector numbers, reads the MBR at its start, mounts partition 1 as `/` and
partition 2 as `/private/var`. A small daemon on the root (in place of `fairplayd.O2`) copies the renderer from
partition 3 to `/var/tmp/j1gl.dylib`, where the loader injected into `backboardd` picks it up.

## 1. Requirements

**Phone**

- Samsung Galaxy S5 **SM-G900M** (`klte`, Snapdragon 801). Other Snapdragon `klte` models (G900F, G900I, G900W8, ...)
  use the same SoC and may work, but only the G900M has been tested; check the partition layout (step 3).
  Variants with a locked bootloader (most US carrier models, e.g. G900A/G900V) cannot boot unsigned images. The
  Exynos models (G900H) are a different SoC and will not work.
- TWRP for `klte` installed in the recovery partition. The developer used TWRP 3.7.0 (`omni_klte`). TWRP has adb with
  root, `dd`, `losetup` and `sha256sum`, which is all the flashing scripts need.
- A battery above 50%. Writing `userdata` over adb takes a while.

**Mac**

- macOS with **Xcode** installed (its clang builds the kernel, the renderer and the reload daemon; `hdiutil`, `ditto`
  and `plutil` come with macOS). Tested on macOS 15.7 with Xcode 26.2 (Apple clang 17). No iPhoneOS SDK content is
  needed: the renderer links against the small stub files in `ports/glshim/sdk/`.
- Homebrew packages: `arm-none-eabi-gcc` and `arm-none-eabi-binutils` (the stub), `android-platform-tools` (adb),
  `ipsw` (optional, to download the firmware and look up its key):

  ```sh
  brew install arm-none-eabi-gcc arm-none-eabi-binutils ipsw
  brew install --cask android-platform-tools
  ```
- Python 3 with `cryptography` (firmware decryption) and `Pillow` (artwork conversion):

  ```sh
  python3 -m pip install --user cryptography Pillow
  ```
- About 10 GB of free disk space.

## 2. Kernel

```sh
git clone https://github.com/Quantitative101/iOS7-KLTE-GalaxyS5
cd iOS7-KLTE-GalaxyS5
bash ports/tools/get-xnu.sh            # clones kOtusin/xnu-j1xlte at e8f2dc9 into ports/xnu-j1xlte, applies the patch
S5=1 bash ports/tools/build-xnu.sh     # about 1-5 minutes
ls -l ports/build/obj-s5/DEVELOPMENT_ARM_EXYNOS3475_J1XLTE/mach_kernel
```

The build ends with errors like `ctf_insert: can't open file: --target=armv7-apple-darwin` and
`*** [...symbolset] Error 1`. These come from steps after the kernel is linked and are expected: what matters is that
`mach_kernel` exists (about 7 MB). If it is missing, look for the first `error:` in `ports/build/make.log`, and check
for `Undefined symbols` there.

Build options go in `J1_KFLAGS`, for example `J1_KFLAGS=-DJ1_S5_CPU_L=0` (keep the bootloader's CPU clock; see the
README's CPU clock section) or `J1_KFLAGS=-DJ1_S5_FILL=0` (letterbox the 480x800 screen instead of stretching it).
`make` does not notice flag changes: `touch` the files that use the flag, or delete `ports/build/obj-s5` and rebuild.

## 3. Check where `userdata` is on your phone

The kernel's storage driver uses fixed sector numbers for `userdata`, taken from the developer's SM-G900M:

```
ports/xnu-j1xlte/xnu/iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7S5SDHCI.cpp
    #define DISK_FIRST 6010880ULL       /* userdata, in eMMC sectors */
    #define DISK_SECTORS 24766431ULL
```

Boot the phone into TWRP, connect USB and compare (*on the phone*):

```sh
adb shell 'ls -l /dev/block/platform/msm_sdcc.1/by-name/userdata'   # should point to mmcblk0p26
adb shell 'cat /sys/block/mmcblk0/mmcblk0p26/start /sys/block/mmcblk0/mmcblk0p26/size'
```

The two numbers must be `6010880` and `24766431`. If they differ, put your values into those two defines and rebuild
the kernel (step 2; `touch` the file first). If `userdata` is not `mmcblk0p26` on your phone, also change `p26` in
`s5/tools/write-userdata.sh`, `s5/tools/write-sector-runs.sh` and in the commands below.

The kernel copies its log to the last 1024 sectors of `userdata` and screenshots to the 4096 sectors below that, so
those offsets also follow from `DISK_SECTORS` (step 10).

## 4. Boot image

Dump your phone's own boot partition in TWRP. It is used only as a container: the build keeps its header and its
QCDT (the device tree table the bootloader picks from) and replaces the kernel. Keep this file: it is also how you go
back to Android.

```sh
mkdir -p backup
adb shell 'dd if=/dev/block/platform/msm_sdcc.1/by-name/boot of=/tmp/boot.img bs=1048576'
adb pull /tmp/boot.img backup/stock-boot.img
bash s5/stub/build.sh backup/stock-boot.img          # -> s5/build/xnu-boot.img (about 10 MB)
```

`s5/stub/build.sh` flattens `mach_kernel`, writes a small Apple-format device tree, builds the stub with
`arm-none-eabi-gcc` and packs them into a copy of the stock image.

While in TWRP, also back up anything you want to keep (TWRP's Backup menu: EFS and modem at least).

## 5. Renderer and reload daemon

```sh
sh ports/glshim/build.sh        # -> ports/glshim/j1shim.dylib (loader) and ports/glshim/j1gl.dylib (renderer)
sh ports/testapp/build.sh       # -> ports/testapp/fbtest (payload reload daemon)
```

`clang: warning: using sysroot for 'MacOSX' but targeting 'iPhone'` is harmless.

## 6. Get and decrypt the iOS root filesystem

The userland is the **iPod touch 5 (iPod5,1) iOS 7.1.2, build 11D257** restore image, downloaded from Apple:

```sh
mkdir -p ports/ipsw/ipod5_1_7.1.2
cd ports/ipsw/ipod5_1_7.1.2
ipsw download ipsw --device iPod5,1 --build 11D257     # or get iPod5,1_7.1.2_11D257_Restore.ipsw from ipsw.me
unzip -j iPod5,1_7.1.2_11D257_Restore.ipsw 058-4385-009.dmg
ipsw download keys --device iPod5,1 --build 11D257     # the RootFS entry (058-4385-009.dmg) has a 72-hex-digit key
cd ../../..
python3 -I ports/tools/vfdecrypt.py ports/ipsw/ipod5_1_7.1.2/058-4385-009.dmg ports/ipsw/ipod5_1_7.1.2/rootfs.dmg KEY
```

Replace `KEY` with the root filesystem key. The IPSW's SHA-1 should be `f935397e...` (as listed by Apple and ipsw.me).
`ports/ipsw/` is in `.gitignore`: never commit or upload anything from it.

To check the result, `hdiutil attach -readonly ports/ipsw/ipod5_1_7.1.2/rootfs.dmg` should mount a volume named
`Sochi11D257.N78OS`; detach it again before the next step.

## 7. Root and /var volumes

```sh
bash s5/tools/build-ipod-root.sh     # -> ports/ipsw/ipod5_1_7.1.2/root-ipod.hfs (1.75 GiB)
bash s5/tools/build-var.sh           # -> ports/ipsw/ipod5_1_7.1.2/var.hfs (640 MiB)
```

`build-ipod-root.sh` makes these changes to the stock root (details in the script):

1. removes the ringtones and the iOS 7 "New" UI sounds (space);
2. keeps the 48 launchd jobs listed in `s5/root/launchdaemons.keep` and parks the others in
   `/System/Library/LaunchDaemons.disabled` (hardware this port lacks makes many daemons hang or crash-loop);
3. applies five byte patches to SpringBoard and three to the dyld shared cache with
   `s5/tools/patch-ipod-binaries.py`, which checks the SHA-1 of every file before and after (the patches and their
   reasons are listed at the top of that script);
4. adds `LC_LOAD_DYLIB /usr/lib/j1shim.dylib` to `backboardd` and installs `j1shim.dylib`;
5. replaces `/usr/sbin/fairplayd.O2` with `fbtest` and makes its launchd job RunAtLoad + KeepAlive;
6. adds a default wallpaper for the device model the kernel reports (`QEMU-virt`) to `DefaultWallpapers.plist`;
7. writes @1x copies of the 2346 images that exist only at @2x, since the S5 runs a 480x800 screen at scale 1.

launchd only loads job plists and binaries owned by root. On the owners-off mount the script uses, new files belong to
your user, so the script overwrites existing files in place (`cat >`) instead of copying new ones.

`build-var.sh` copies the root filesystem's own `/private/var` into a fresh 640 MiB HFS+ volume and adds
`com.apple.migration.plist` (LastSystemVersion `11D257`) for root and mobile, without which `backboardd` waits for a
data migration on first boot.

## 8. Userdata image

```sh
E=ports/ipsw/ipod5_1_7.1.2
python3 -I ports/tools/make-disk.py $E/root-ipod.hfs $E/var.hfs s5/build/userdata.img
python3 -I ports/tools/gl-push.py s5/build/userdata.img ports/glshim/j1gl.dylib
```

`make-disk.py` writes an MBR with three type-0xAF partitions: root at sector 2048, `/var` after it, and a 2 MiB
payload partition after that. `gl-push.py` stores the renderer in the payload partition with a header (`J1GL`,
sequence number, length); the reload daemon copies it whenever the sequence number changes. The image is about
2.4 GiB.

## 9. Flash

Boot into TWRP and connect USB. Check that `adb devices` lists the phone and that `adb shell` gives a root prompt.

**Userdata.** This erases Android's data, including the internal storage TWRP shows as `/sdcard`.

```sh
bash s5/tools/write-userdata.sh s5/build/userdata.img
```

The script unmounts `/data`, then writes the image in 128 MiB pieces: it pushes each piece to the phone's `/tmp`,
writes it through a loop device at the right offset (TWRP's busybox `dd seek=` does not work on this phone) and
compares SHA-256 sums. If it stops (cable, TWRP restart), rerun it with the piece number it reports as the second
argument, e.g. `bash s5/tools/write-userdata.sh s5/build/userdata.img 7`.

**Boot.**

```sh
adb push s5/build/xnu-boot.img /tmp/xnu-boot.img
adb shell 'dd if=/tmp/xnu-boot.img of=/dev/block/platform/msm_sdcc.1/by-name/boot bs=1048576 conv=fsync'
shasum -a 256 s5/build/xnu-boot.img
adb shell "dd if=/dev/block/platform/msm_sdcc.1/by-name/boot bs=2048 count=$(( $(stat -f%z s5/build/xnu-boot.img) / 2048 )) 2>/dev/null | sha256sum"
```

The last two lines should print the same hash (the image is a whole number of 2048-byte pages, so the read-back
covers exactly the image).

## 10. Boot and read the logs

In TWRP choose Reboot > System. The screen shows the kernel's text console, then the iOS lock screen. Swipe to unlock;
the Home key works.

Every session ends back in TWRP: the stub sets the restart reason to recovery and arms the watchdog, and the kernel
stops feeding it after 10 minutes or as soon as it hangs or panics. You can also hold Power + Volume Down to reset, and
Power + Volume Up + Home to enter TWRP.

The kernel log and the last frame are on `userdata`. With `S` = your `DISK_SECTORS` (24766431 on the G900M), the log
starts at userdata sector `S - 1024` and the frame dump at `S - 5120`. Read them through a loop device: `dd skip=` in
TWRP reads through all the skipped data, which takes minutes. On the G900M:

```sh
adb shell 'losetup -o $((24765407 * 512)) /dev/block/loop7 /dev/block/mmcblk0p26 && dd if=/dev/block/loop7 of=/tmp/ring.bin bs=512 count=520; losetup -d /dev/block/loop7'
adb pull /tmp/ring.bin && python3 -I s5/tools/ringbin.py ring.bin > ring.txt
adb shell 'losetup -o $((24761311 * 512)) /dev/block/loop7 /dev/block/mmcblk0p26 && dd if=/dev/block/loop7 of=/tmp/shot.bin bs=512 count=3001; losetup -d /dev/block/loop7'
adb pull /tmp/shot.bin && python3 -I s5/tools/shot.py shot.bin shot.png
```

TWRP may restart by itself 6-7 minutes into an adb session after an iOS run (the watchdog the stub armed is still
running). Reconnect and carry on.

## 11. Updating one part

- **Renderer only**: rebuild it, wrap it with a new sequence number and write it to the payload partition (MBR
  partition 3; `make-disk.py` printed where partition 2 starts and ends, and partition 3 starts at the next 2048-sector
  boundary). Partition 3's start sector can also be read back from `s5/build/userdata.img`:

  ```sh
  sh ports/glshim/build.sh
  python3 -I s5/tools/mkpl.py ports/glshim/j1gl.dylib $(printf %x $(date +%s)) s5/build/pl.bin
  P3=$(python3 -I -c 'import struct; print(struct.unpack_from("<I", open("s5/build/userdata.img","rb").read(512), 446+32+8)[0])')
  echo "0 $(( $(stat -f%z s5/build/pl.bin) / 512 ))" > s5/build/pl.runs
  bash s5/tools/write-sector-runs.sh s5/build/pl.bin s5/build/pl.bin $P3 s5/build/pl.runs
  ```
- **Kernel only**: rebuild (step 2), rerun `s5/stub/build.sh backup/stock-boot.img`, flash the boot partition (step 9).
- **Root volume**: rebuild `root-ipod.hfs`, then either rewrite all of `userdata` or write only the changed sectors:
  compare the old and new volume images (`s5/tools/secdiff.py`) and write the runs with `s5/tools/write-sector-runs.sh`
  at base sector 2048.

## 12. Back to Android

In TWRP, write your stock boot image back and format data (Wipe > Format Data), which recreates Android's
`userdata` file system:

```sh
adb push backup/stock-boot.img /tmp/boot.img
adb shell 'dd if=/tmp/boot.img of=/dev/block/platform/msm_sdcc.1/by-name/boot bs=1048576 conv=fsync'
```

## How this differs from the developer's phone

The volumes built by this guide were checked file by file against the ones that ran on the developer's phone:

- The root volume matches, except that it carries this checkout's `j1shim.dylib` and `fbtest` builds and not two
  leftover files (an unused `fairplayd.A1` and its parked job) from earlier experiments.
- `var.hfs` matches the `/var` volume first written to the phone. On the phone, `/var` then built up state across
  many boots, so a fresh first boot with this volume has not itself been tested; expect the first boot to take longer
  while caches are created.
- The developer's `userdata` layout differs (the root sits at a higher sector after a move from the iPad root). The
  kernel only reads the MBR, so the layout of `make-disk.py` works the same way.

## Troubleshooting

- **`mach_kernel` missing**: the first `error:` in `ports/build/make.log` is the cause. A flag change without a clean
  rebuild can leave stale objects.
- **`make-1x-images.py needs Pillow`**: install it for the `python3` on your `PATH` (`python3 -m pip install --user
  Pillow`). That step runs without `-I` so that user-installed packages are found.
- **`patch-ipod-binaries.py`: SHA-1 ... expected the unmodified 11D257 iPod5,1 file**: the root filesystem is not the
  iPod5,1 11D257 one, or was decrypted incorrectly.
- **`write-userdata.sh` says `userdata still mounted`**: unmount Data in TWRP's Mount menu and rerun.
- **Black screen with the console gone, or no lock screen after several minutes**: read the log (step 10). A log line
  `gl missing` means the renderer payload was not installed: check that partition 3 holds the `J1GL` header.
- **Phone does not come back to TWRP**: hold Power + Volume Up + Home until the Samsung logo, then let go of Power.
