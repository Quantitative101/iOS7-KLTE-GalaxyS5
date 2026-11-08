# iOS7-KLTE-GalaxyS5

iOS 7 for the Galaxy S5!

The iOS 7.1.2 userland of an iPad 2 (iPad2,2, build 11D257) running on an ARMv7 port of Apple's XNU 2107 kernel, on a
Samsung Galaxy S5 SM-G900M (`klte`, Qualcomm MSM8974PRO-AC / Snapdragon 801).

> **Experimental.** This is a research port, not something to run on a phone you depend on. Flashing it replaces the
> boot partition and needs a root filesystem written over `userdata` (all Android data on it is lost). You need to be
> comfortable restoring the phone from TWRP yourself.

This repository contains only code written for this port, and modified open-source sources. Nothing from Apple's or
Samsung's binaries is here: no IPSW contents, root filesystem, dyld shared cache, SDK, boot images, device trees or
firmware. You supply those from your own device and your own downloads.

## Status

Works:

- Boot from Samsung's LK bootloader through a small stub (`s5/stub/`) in a clone of the phone's own boot image
- eMMC storage (SDHCI), root filesystem on the `userdata` partition
- launchd, backboardd, SpringBoard: lock screen, unlock, home screen
- Settings, Mail, Contacts open and run
- Touch (Synaptics over bit-banged I2C) and the Home key (PMA8084 GPIO over SPMI)
- Software OpenGL ES renderer injected into backboardd (`ports/glshim/`), with a fast path for axis-aligned
  rectangles (most CoreAnimation layers)
- Data cache enabled; CPU0 clock raised from 960 MHz to 1.5 GHz (see below)
- CPU1 woken as a helper core that mirrors finished frames to the command-mode panel; XNU itself still runs on one
  core (plan for real SMP in `docs/SMP-SCOPE.md`)

Not working yet: on-screen keyboard (the iPad2,2 root has no layouts for the phone-class screen), Photos (assetsd
crash loop), Wi-Fi, cellular, audio, sensors, real multi-core scheduling.

## Layout

| Path | What |
|---|---|
| `xnu/s5-xnu-j1xlte.patch` | All kernel changes, against [kOtusin/xnu-j1xlte](https://github.com/kOtusin/xnu-j1xlte) commit `e8f2dc9` |
| `ports/tools/get-xnu.sh` | Clones that base into `ports/xnu-j1xlte` and applies the patch |
| `ports/tools/build-xnu.sh` | Kernel build with Xcode clang (`S5=1` selects the Galaxy S5 variant); `stubs/` are its host-tool stand-ins |
| `ports/tools/` | Mach-O flattener, Apple device tree writer, root image helpers (HFS extraction, disk assembly, dylib injection), dyld-cache symbol tools |
| `ports/glshim/` | Software GLES renderer and the loader injected into backboardd; `test/` has a native self-test and benchmark |
| `ports/testapp/` | Guest helper that reloads the renderer payload at boot |
| `s5/stub/` | Bare-metal stub LK starts as the "kernel": sets the recovery restart reason, arms the watchdog, jumps to XNU |
| `s5/tools/` | Boot image cloner (keeps the QCDT), log ring and frame dump decoders, userdata writer, TWRP probes |
| `docs/DEVLOG.md` | The full development log (run by run), including the root filesystem fixes |

## Building

Requirements: macOS with Xcode (its clang builds the kernel and the renderer), `arm-none-eabi-gcc`/`binutils` for the
stub, Python 3.

```sh
bash ports/tools/get-xnu.sh                 # fetch the base tree and apply the S5 patch
S5=1 bash ports/tools/build-xnu.sh          # -> ports/build/obj-s5/DEVELOPMENT_ARM_EXYNOS3475_J1XLTE/mach_kernel
bash s5/stub/build.sh /path/to/your/stock-boot.img   # -> s5/build/xnu-boot.img
```

`build-xnu.sh` prints `done` and the first error-looking lines of `ports/build/make.log`; errors from the late
`ctf_insert` / symbol-set steps are expected (the kernel is linked before them). `s5/stub/build.sh` flattens the
kernel, writes a minimal device tree and puts both, with the stub, into a copy of your stock boot image, keeping its
header and QCDT (LK chooses the DTB from it).

The renderer (`ports/glshim/build.sh`) links against an iPhoneOS SDK sysroot in `ports/glshim/sdk/`, which is not
included: take the needed libraries/frameworks from Xcode's iPhoneOS SDK.

## What you supply

- **Your phone's own stock boot image** (dump `boot`, mmcblk0p15, from TWRP). It is only used as a container.
- **The iPad2,2 iOS 7.1.2 (11D257) IPSW**, from Apple. Its decrypted root filesystem becomes the iOS root.
- **The root filesystem setup**: the root and `/private/var` volumes go into an MBR disk image (`ports/tools/make-disk.py`)
  written to `userdata` (`s5/tools/write-userdata.sh`), plus the renderer payload and a few fixes to the root (launchd
  jobs, SpringBoard and MobileGestalt adjustments). These steps are described in `docs/DEVLOG.md`; there is no
  one-command script yet.

## Flashing and recovery

From TWRP (adb shell), write `xnu-boot.img` to the `boot` partition with `dd` and read it back to compare checksums.
Keep a copy of your original boot image so you can put Android's kernel back the same way.

The stub sets the restart reason to *recovery* first thing and arms the APPS watchdog. The kernel pets it for the first
10 minutes of a session; after that, or as soon as the kernel hangs or panics, the watchdog resets the phone into TWRP.
Every session therefore ends in TWRP, where the kernel's log ring (`s5/tools/ringbin.py`) and the last frame
(`s5/tools/shot.py`) can be read back from `userdata`.

## CPU clock

LK leaves CPU0 at 960 MHz (HFPLL L=50 at 19.2 MHz) with its own core voltage. Once backboardd is up, the kernel
(`IOS7S5Touch.cpp`, `ios7s5_cpu_clock`) moves CPU0 to **1497.6 MHz (L=78)** without touching the voltage, then runs a
fixed integer/memory self-test 8 times and compares the results with a run at the boot clock.

1728 MHz (L=90) was stable on the developer's phone (self-test 8/8, a full session), but it runs above the frequency
the bootloader's voltage is meant for, which is effectively an undervolt at that frequency. On other units it may crash
or silently corrupt computations. It cannot damage the hardware, but you use it at your own risk:

```sh
J1_KFLAGS=-DJ1_S5_CPU_L=90 S5=1 bash ports/tools/build-xnu.sh
```

`make` does not track flag changes: when rebuilding an existing tree, touch
`ports/xnu-j1xlte/xnu/iokit/Drivers/KernelBuiltIn/ARM/AppleARMPlatform/IOS7S5Touch.cpp` first.
`J1_KFLAGS=-DJ1_S5_CPU_L=0` keeps the bootloader's clock and only logs it.

## Credits

- [kOtusin/xnu-j1xlte](https://github.com/kOtusin/xnu-j1xlte): the ARMv7 XNU 2107 port for the Galaxy J1 this kernel
  builds on, itself based on the [HTC Leo revival XNU](https://github.com/HTC-Leo-Revival-Project/xnu).
- Apple's open-source XNU; mainline Linux (`drivers/clk/qcom`, `arch/arm/mach-qcom`, MSM8974 device trees) as the
  hardware reference.

## License

See `LICENSE`. The kernel patch modifies Apple's XNU and is under the Apple Public Source License 2.0; everything else
written for this port is MIT. iOS, iPad and XNU are Apple trademarks; Galaxy is a Samsung trademark. This project is not
affiliated with Apple, Samsung or Qualcomm.
