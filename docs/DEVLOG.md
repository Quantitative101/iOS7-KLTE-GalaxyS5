# Galaxy S5 port: development log

The raw working notes kept during the port, newest sections at the end. Paths such as `s5/build/...`, `ports/build/disk.img`
or `ports/ipsw/...` refer to the developer's local tree: boot images, payloads, root filesystem images, logs and
anything from Apple's or Samsung's firmware are not part of this repository. Run numbers count test boots on the phone.

## S5 next session (written overnight 2026-11-07/08, phone offline)

## State on the phone (run 100, flashed 2026-11-08: keys boot + pl96)
Backup of the previous boot partition (b7ea10be, full 13 MiB p15): `s5/build/boot-p15-b7ea10be.full.img` sha1 d13ae046.

### Before run 100
- Boot (mmcblk0p15): `s5/build/xnu-boot.img` sha1 b7ea10be (backboardd mach-queue overflow, touch driver, RPM,
  /chosen display-scale). No kernel changes since; nothing to reflash there.
- GL payload (userdata byte offset 2186280960): pl89 sha1 413bf7dc.
- Root: installd plist restored, SpringBoard thermal/idle patches (local `ports/build/disk.img` root matches).
- var: MobileGestalt cache CacheVersion "11D25X" (forces rebuild with scale 1).

## Payloads built overnight (all in `s5/build/`, 425 sectors each unless noted)
| payload | sha1 | contents |
|---|---|---|
| pl90 | 60d4174f... (392 sectors) | pl89 + scatter animated:NO |
| pl91 | 37a5776b... | pl90 + fast renderer |
| pl92 | 3d50cb20... | pl91 + `~ipad` icon fallback (NSBundle, SpringBoard only) + ICONMISS/ICONFB logs |
| **pl93** | 95ad1bfb616ff77a7db2e5355e7556c62e658ce5 | pl92 + backboardd `+[CADisplay mainDisplay]` retry |
| **pl94** | 93d3e6d934917a882e38b8c0b53334d4f8b650fb | pl93 without the SpringBoard scatter/unscatter animated:NO overrides |
| **pl95** | ab859128d8ef819454505e629a769d216136c869 (426 sectors) | pl93 + Home button watcher (needs the keys boot image) |
| **pl96** | 9831408215f9bb57bc68e20eaf8ce36b114d945e | pl94 + Home button watcher (needs the keys boot image) |

## Boot images built overnight (`s5/build/`)
Back up the current boot partition first (it holds b7ea10be, which is not saved locally):
`adb shell "dd if=/dev/block/mmcblk0p15 of=/tmp/boot-b7ea10be.img bs=1048576"` then `adb pull` it to s5/build/.

- **`xnu-boot.keys.img`** sha1 402bb6eacc7fe7e7c4b1c0c3d63a19ea25ec7a55 (preferred) = b7ea10be +
  - Home / volume keys: PMA8084 GPIO 3/2/5 (active low) read every 100 ms over SPMI (arbiter v1 0xfc4cf000, apps
    channel 0 at +0x800, EXT_READL, STATUS1 bit 0), read only, ONLY once backboardd has opened the touch service (so
    the first, untested SPMI access happens with the UI up and the log ring running). Published as `J1Keys` /
    `J1HomePresses` on IOS7J1Touch; pl95/pl96's SpringBoard thread turns each new Home press into
    `-[SBUIController clickedMenuButton]` ("HOMEBTN" log). Logs: `S5KEY: pin c200 read=0 status1=.. mode=.. pull=..
    en=..` once (read != 0 = SPMI refused, keys stay off, no side effects), then `S5KEY: keys a -> b home presses n`.
  - Touch reads gated by the touch interrupt line (PMA8084 GPIO 8, active low, SPMI): while idle with no finger
    down the ~1.5 ms bit-banged I2C read is skipped (~7% CPU at idle); every 10th poll (200 ms) reads anyway. Logs
    `S5TOUCH: irq line a -> b (poll n)` (expect 0 idle, 1 on touch). If touch turns sluggish (200 ms updates), the line
    does not behave: use the nospmi image.
- **`xnu-boot.nospmi.img`** sha1 206077646dfa01b78306187c942196bcb881bd30 = the same kernel built with
  `J1_KFLAGS=-DJ1_S5_SPMI=0`: no SPMI access at all (no keys, touch polled every 20 ms as in b7ea10be).
Flash: `dd if=<img> of=/dev/block/mmcblk0p15 bs=1048576 conv=fsync`, read back `count=$(( (size + 4095) / 4096 ))`
with bs=4096 and compare sha1.

### Why pl93's mainDisplay retry matters (found offline in runs 84-96 logs)
backboardd aborts on the first touch with `Assertion failed: (BKDisplayGetMainWindowServerDisplay())` in
`shiftDigitizerLocationToHitContext` (BKDigitizer.m:447) in EVERY backboardd whose display lookup had to wait, and in
none that did not. `BKDisplayStartWindowServer` reads `[CADisplay mainDisplay]` before `displayWithDisplayId:`; when it
is nil the (retried) lookup still succeeds, but backboardd branches past the only store of the main window server
display global (`str r0,[r6,#0x14]` at 0xadac, global 0x5fa94) and the first touch then asserts. Retrying
`mainDisplay` itself (log `MAINDISP waits ptr`) fixes it at the source. This is the old "touch works once, then
everything restarts".

### Why pl94 exists
The scatter/unscatter `animated:NO` overrides were added when there were no icons (zoom animator divided by an icon
count of 0). Now installd provides icons; forcing a non-animated path into code built around the animation is a
plausible source of the run 99 SpringBoard heap corruption (QuartzCore `slab->magic == SLAB_MAGIC` assert at release,
right after a tap on an icon).

## Test plan
1. Back up p15, then flash `xnu-boot.keys.img` + pl96 (no animation overrides, Home button, interrupt-gated touch). Unlock, tap an icon (Notes or Photos), press the
   physical Home key to go back, tap more icons. (Without the new boot image, use pl94 / pl93 instead.)
   - If SpringBoard aborts with `CALayer position contains NaN` from SBCenterIconZoomAnimator again: flash pl95.
   - If it aborts with the slab assertion anyway: corruption is elsewhere (next suspects: the NSBundle icon fallback
     (try pl91 + mainDisplay patch), then the port itself).
2. Check the log for `MAINDISP` (waits) and that no `BKDigitizer.m, line 447` assertion appears.
3. Check `ICONFB-OK` / `ICONMISS` lines: if the template icons are not fixed and only ICONMISS shows, icons are looked
   up through CF, not NSBundle: then provide the missing iPhone-named icon files on the root (HFS workflow in memory).

```
P=s5/build/pl94.bin; N=$(( $(wc -c < $P) / 512 ))
adb push $P /tmp/pl.bin
adb shell "L=\$(losetup -f); losetup -o 2186280960 \$L /dev/block/mmcblk0p26 && dd if=/tmp/pl.bin of=\$L bs=512 conv=fsync; sync; echo 3 > /proc/sys/vm/drop_caches; dd if=\$L of=/tmp/rb.bin bs=512 count=$N; losetup -d \$L; sha1sum /tmp/rb.bin" </dev/null
adb reboot
```
(Inside shell loops give adb `</dev/null`; strip `\r` before comparing hashes. Log ring: userdata byte 12679888384,
520 sectors; `python3 s5/tools/ringbin.py`.)

## Renderer speedup (pl91+), `ports/glshim/render.h`
- `r_rect`: two triangles / 4-vertex strip tiling an axis-aligned rectangle (CA layers, images, glyph quads) filled
  directly with per-column/row texel tables; integer copy (opaque TEX / white MOD), integer premultiplied blend,
  opaque flat fill. Within +-2 per channel of the float path.
- `r_tri`: fixed-point (8 sub-pixel bits) incremental edge functions, exact top-left fill rule.
- `r_blend_px`: CA's two common blend modes without the per-channel factor switch (exact).
- Native benchmark: `sh ports/glshim/test/bench.sh` (home-screen-like frame vs the reference path): 8.6 -> 0.77 ms on
  the Mac (~11x); the only >2 differences are nearest-sampling ties in a deliberately magnified mask.
- `J1_CFLAGS=... sh ports/glshim/build.sh` passes extra compiler flags (used for pl94).

## Known open items
- Template icons (FaceTime, Calendar, Reminders, Settings): apps ship only `~ipad` icon files; 480x800 makes iOS treat
  the device as iPhone-class. pl92+ tries an NSBundle fallback.
- No wallpaper (black), dock blur approximated, clock fixed (RTC: PMA8084 at SPMI 0x6000 via arbiter 0xfc4cf000, but
  qcom,qpnp-rtc-write=0 so it likely counts from an arbitrary epoch; not attempted).
- Mail starts in the background at boot: a running MobileMail does NOT mean a tap launched it.

## Run 100 result (keys boot + pl96) and run 101 (flashed 2026-11-08)
- Run 100: Home key works (S5KEY reads fine, HOMEBTN -> clickedMenuButton). Settings and Contacts open.
- "Notes crashed SpringBoard": not Notes. On Home, backboardd calls `pid_shutdown_sockets` (syscall 436) on the app it
  suspends (Preferences, pid 66); the kernel had no 436 (CONFIG_EMBEDDED off) -> SIGSYS -> backboardd dies ->
  SpringBoard exits. Fix: 436 is a no-op success in `bsd/dev/arm/unix_syscalls.c` (like 435).
- Photos: MobileSlideShow SIGABRT; its assetsd aborts every launch on an Objective-C exception thrown from
  PhotoLibraryServices (reason unknown). Run 101 injects j1shim into assetsd and MobileSlideShow (kern_exec.c) with
  only the exception logger (eagl.c branch; EXCNEW-NAME/REASON/BT lines), SIGPIPE exempt (sys_generic.c).
- Run 101 images: boot `s5/build/xnu-boot.r101.img` sha1 d2caa87f, payload pl97 sha1 47920c42 (pl96 + that branch).

## Run 101 result and run 102 (flashed 2026-11-08)
- Run 101: Home dead. S5KEY first read had Home low and it never changed: pin c200 pull register = 4 (10 uA
  pull-down) while the stock DT says qcom,pull = <0> (30 uA pull-up). Run 102 writes 0 to 0xc242 once over SPMI
  (EXT_WRITEL, WDATA0 +0x10) and logs `S5KEY: pin c200 pull-up write=.. pull=.. status1=..`.
- User reports: apps open in a tiny centred window; Settings shows raw keys (DO_NOT_DISTURB, CASTLE) or blank labels.
  - Tiny window = SpringBoard classic (letterbox) mode: pl98 swizzles -[SBApplication isClassic] -> NO (CLASSIC log).
  - Labels: Preferences.app has only ~ipad string tables (Settings~ipad.strings, Localizable~ipad.strings). Run 102
    injects j1shim into every /Applications or /var/mobile/Applications executable; the shim's app branch
    (APPSHIM log) installs the exception logger, the ~ipad image fallback and a
    -[NSBundle localizedStringForKey:value:table:] retry with table "~ipad" (STRFB-OK), and sets F_SETNOSIGPIPE on
    stderr. No GL hooks in apps.
  - Root model in the XNU DT is "QEMU-virt"/"AppleARM" (ports/uniloader/blob/dtb): that is why CFBundle and
    MobileGestalt do not treat the device as an iPad. Changing it to iPad2,2 would flip SpringBoard to the iPad idiom.
- The shim did not load into assetsd in run 101 (no J1LOADER line): dyld or the exec path skipped it. Photos still open.
- Images: boot `s5/build/xnu-boot.r102.img` sha1 ae73cee9, payload pl98 sha1 b506df83 (426 sectors).

## Run 102 result and run 103 (flashed 2026-11-08)
- Run 102: Home works (pull-up write ok: pull 04 -> 00, presses counted). Settings labels fixed, apps full screen
  (CLASSIC was YES -> NO). Settings flickered black once at launch, then settled. Contacts fine.
- Notes: SIGBUS at address 0 in CoreImage, called from CoreUI drawing (UIKit), right after the app opened the GPU
  (J1GPU open type=1): a GPU CIContext with no GL driver in apps. pl99 forces CoreImage's CPU renderer in apps
  (+[CIContext contextWithOptions:] / contextWithEAGLContext:(options:) / -initWithOptions: get
  kCIContextUseSoftwareRenderer = YES; log CIFIX).
- App stderr was never logged (J1WRITE only covered backboardd/SpringBoard/fairplayd): run 103 logs fd 2 writes of
  every process (APPSHIM, STRFB-OK, EXCNEW-* from apps should now show).
- MobileGestaltHelper still dies with SIGBUS (pc in libMobileGestalt) several times per boot: unexplored.
- Images: boot `s5/build/xnu-boot.r103.img` sha1 62e94127, payload pl99 sha1 8051124d (426 sectors).

## Run 103 result and run 104 (flashed 2026-11-08)
- Run 103: Settings no longer flickers. Notes and Photos crash. ~2 fps felt.
- Photos: assetsd throws NSInternalInconsistencyException "Application should be allowed to access the photo
  service, but can't"; MobileSlideShow NSObjectInaccessibleException "This application is not allowed to access
  Photo data". Suspect entitlements: csops CS_OPS_ENTITLEMENTS_BLOB requires CS_VALID. Run 104 (J1_S5) returns the
  blob without CS_VALID and logs `J1ENT pid .. by .. csflags= err= len=` (first 40).
- Notes: same CoreImage NULL-GL-table crash with the CPU CIContext (CIFIX logged): CoreUI creates its own
  EAGLContext. pl100 gives apps the software EAGL/GL (app branch falls through to the EAGL hooks).
- Speed: the whole ARM kernel is -O0 (makedefs/MakeInc.def CFLAGS_*ARM = -O0). Timer samples (S4TK): ~1/3 idle,
  ~15% in -O0 hex/console printing and the -O0 present/mirror loops. Run 104 moves the per-frame pixel loops to
  pexpert/arm/s5_blit.c built -O2 (s5_blit.o_CFLAGS_ADD in pexpert/conf/Makefile.template); pl100 logs only the
  first 200 EAGL calls (swapNotification was logged every frame).
- Images: boot `s5/build/xnu-boot.r104.img` sha1 83a7e3ac, payload pl100 sha1 e804f996 (426 sectors).

## Run 104 result and run 105 (flashed 2026-11-08)
- Run 104: not noticeably faster. Settings crashed with the QuartzCore `slab->magic == SLAB_MAGIC` assert (x-alloc.c:567)
  once apps had the software EAGL/GL: the shim GL corrupts CA's heap in-process (the run 99 SpringBoard assert may be
  related). pl101 removes GL from apps again and makes -[EAGLContext initWithAPI:(sharegroup:)] return nil there
  (EAGLNIL log).
- Entitlements were fine (J1ENT: csflags=1, blobs returned); the CS_VALID bypass is harmless but not the Photos fix.
- Photos root cause: tccd grants by issuing a sandbox extension (mac_syscall "Sandbox" call 5), which returned
  ENOPOLICY. Run 105 fakes issue (5, 40-byte struct, token written to a[4]), consume (6, handle 1 to a[2]) and
  release (7) (security/mac_base.c, J1_S5; log `J1SANDBOX ext call`).
- pl101 logs Settings strings that still miss after the ~ipad retry (STRMISS / STRMISS-T, first 30).
- Images: boot `s5/build/xnu-boot.r105.img` sha1 11e33195, payload pl101 sha1 5eb56a8d (427 sectors).

## Run 105 result and run 106 (payload only, flashed 2026-11-08; boot stays r105 11e33195)
- Photos permission now passes (J1SANDBOX ext call 5 by tccd / 6 by assetsd, err 0). assetsd then raises
  NSInvalidArgumentException "Error validating url for store" and "NSPersistentStoreCoordinator has no persistent
  stores" (Photos.sqlite). pl102 logs the store URL and error (PSTORE-URL / PSTORE-ERR). MobileSlideShow was then
  SIGKILLed by backboardd (launch watchdog: too slow).
- Settings > General: SIGBUS far 0xc in QuartzCore's recursive layer-tree walk (no GL in apps). With run 104's
  slab assert in Settings and run 99's in SpringBoard: CA heap/memory corruption in client processes independent of
  the shim GL. Next suspects: kernel VM (J1REMAP mach_vm_remap copy handling, shared memory with backboardd).
- STRMISS: mostly keys equal to their English value ("General", "About"): harmless.
- Notes: same CoreImage crash; EAGLNIL never logged. pl102 also nils initWithAPI:properties: and
  initWithAPI:sharedWithCompute:.
- Payload pl102 sha1 bfb66d8d (427 sectors).

## Renderer profiling (run 107, built 2026-11-08, not yet flashed)
- pl103 (-O1, sha1 e6f6aac9, 459 sectors) / pl104 (-O2, sha1 1da469f8, 460 sectors): pl102 + PROF line every 32
  frames in backboardd (ticks in draw / rect / rejected rect / tri / clear / texture upload / readpixels, counts and
  pixel totals). ports/glshim/build.sh now takes J1_OPT (default 2). mach_absolute_time resolved via dlsym (the SDK
  stubs lack it).
- Boot r107 (sha1 1e98e46d, 10182656 bytes: read back count=2486): kernel present+mirror ticks and present interval
  on the S5GUI line every 64 frames ("prof P I").
- Decode: `python3 s5/tools/prof.py s5/logs/ringNNN.txt`.

## Run 106 result; run 107 flashed 2026-11-08 (boot r107 1e98e46d + pl104 -O2 profiler)
- Photos: assetsd opens file:///var/mobile/Media/PhotoData/Photos.sqlite and gets Cocoa error 260 (file not found);
  the library database was never created (normally made on first-boot setup/migration, which we skip). Next: create
  it (empty file or let assetsd create it) on the var partition via the HFS workflow.
- Notes: EAGLNIL fired (initWithAPI:properties: -> nil), yet CoreImage still calls through the nil context's GL table
  (same pc). Needs CoreUI to avoid CoreImage's GPU path, or a working per-app GL; not solved.

## Run 107 profile result and run 108 (flashed 2026-11-08, boot only; payload stays pl104)
- Kernel profile (S5GUI prof): present + panel mirror took 350-450 ms per frame (up to 1 s), presents arrive about
  once a second: the strongly-ordered panel framebuffer mapping (pmap_map_bd ignores flags: TEX/C/B = 0) makes the
  up-to-5.8 MB mirror write very slow. Shim PROF lines never printed: CA calls attachImage only on (re)attach, not per
  frame, so j1_log_frame is not a frame clock (move PROF to a per-frame hook if needed).
- Run 108 (boot sha1 b1b4e0e6, 10182656 bytes, read back 2486): J1_S5 maps the panel framebuffer (s4_early_fb)
  Normal non-cacheable (TEX=001; l2_map_linear_range_wc in arm_vm_init.c, pmap_map_bd honours VM_WIMG_WCOMB), the
  mirror prefers that mapping, and the 480x800 j1_pixels shadow copy (only for the eMMC frame dump) runs every 8th
  frame. Compare the S5GUI prof numbers with run 107. If the panel shows stale/garbled content: cache/ordering issue
  with the new mapping (fall back to r107 1e98e46d).

## Run 108 result and run 109 (flashed 2026-11-08)
- Run 108: renders correctly with the normal-non-cacheable panel mapping. present+mirror 350-450 -> ~185-230 ms per
  frame, but ~185 ms even for idle frames: the panel writes are not the remaining cost.
- Run 109 (boot sha1 09992449; payload pl105 sha1 64c638bf, 460 sectors): the S5GUI line also carries hash / expand /
  panel-write / shadow-copy ticks and changed rows; the shim's PROF window now ticks on
  -[EAGLContext swapNotification:forTransaction:onLayer:] (every frame). `python3 s5/tools/prof.py` decodes both.

## Run 109 profile and run 110 (flashed 2026-11-08, boot only; payload stays pl105)
- Run 109 profile: backboardd needs 1.4-5 s per frame for 17-78 draws / 30-215 triangles (all in r_tri; the rect
  path never matches CA's quads here); kernel row hashing ~210 ms and the 1.5 MB shadow copy ~220 ms per frame even
  with 0 changed rows. Both read the CA frame surface at uncached speed.
- Root cause: the surface is created with IOSurfaceCacheMode 0x400 (write-combine); VM_WIMG_WCOMB includes
  VM_MEM_NOT_CACHEABLE, and pmap_set_cache_attributes made every such page strongly-ordered for all mappings.
- Run 110 (boot sha1 84ee101a): J1_S5 maps VM_WIMG_WCOMB pages write-through cacheable like ordinary pages (no device
  reads them; the panel scans out the separate buffer the kernel fills). Expect large gains in both PROF and S5GUI
  prof. If the screen shows stale content: coherency problem (fall back to r109 09992449).
- Mail's account logos (iCloud, Exchange, Outlook.com) render sheared: texture row-pitch mismatch for some images
  (the 32-byte BGRA padding rule in glTexImage2D does not fit them). Not fixed yet.

## Run 110 result and run 111 (flashed 2026-11-08, boot only; payload stays pl105)
- Run 110: no change (kernel frame read still ~200 ms, backboardd 1.4-2 s per frame). The WCOMB pmap fix is harmless
  but was not the cause.
- Real cause: the ARM data cache was never enabled. s5/stub/stub.S clears SCTLR.C/I/M before entering XNU; locore
  ("Enable unaligned memory access and caching") sets only the I-cache bit (12), never C (bit 2). Every load/store in
  the kernel and every process went to RAM uncached (~1000x slower renderer than native).
- Run 111 (boot sha1 43167d1b): J1_S5 PE_init_SocSupport_stub calls s5_enable_dcache(): invalidate all data/unified
  levels by set/way (CLIDR/CCSIDR loop; L1 + Krait L2 may hold stale lines from Android) while C=0, then set SCTLR.C.
  Logged as `S5DCACHE sctlr ...` (bit 2 set = on) before the first S5GUI frame. J1_KFLAGS=-DJ1_S5_DCACHE=0 disables.
  Safety basis: eMMC driver is PIO (no DMA), panel framebuffer / log ring / MMIO mappings are non-cacheable, kernel
  and user pages are write-through. Fallback: r110 84ee101a.

## Run 111 result and run 112 (flashed 2026-11-08, boot only)
- Run 111: data cache on (S5DCACHE sctlr 00c5587d, C set), renders fine, still slow: frame hash still ~210 ms
  (~0.55 us per word, slower than uncached RAM should be) and backboardd still ~1.4-2 s per frame. Suspect CPU clock.
- Run 112 (boot sha1 f24d30aa): PMU cycle counter vs 19.2 MHz CNTPCT over a 10M-iteration busy loop, printed on the
  S5DCACHE line ("clock cycles,ticks"). MHz = cycles / ticks * 19.2.

## Run 112 result; run 113 built (not flashed yet)
- Run 112: CPU clock ~960 MHz (10M-iteration loop: 20000277 PMU cycles in 400008 ticks of 19.2 MHz). Not the cause.
  The frame hash costs ~520 cycles per word read: the frame memory still behaves as uncached (slow bus).
- Run 113 (boot sha1 37df684a; the first r113 attempt failed to link and was identical to r112, which got re-flashed):
  logs `S5PAR` for the first 3 presents: PAR after ATS1CPR for the frame and for kernel text (inner attrs bits 6:4:
  001 strongly-ordered, 011 device, 000 non-cacheable, 101 WB-WA, 110 WT, 111 WB), plus hash ticks over 1.5 MB of
  each (cached reference).

## Run 113 result and run 114 (flashed 2026-11-08, boot only)
- Run 113 S5PAR: frame and kernel text both PAR ...268 = inner+outer write-through cacheable, and both hash 1.5 MB in
  ~4.7M ticks of 19.2 MHz (~245 ms; the profiler's ms figures assumed 24 MHz, multiply them by 1.25). Cached
  write-through memory is as slow as uncached: Krait apparently treats WT as non-cacheable. This port maps all RAM
  WT (kernel linear map l2_map_linear_range, pmap_get_cache_attributes for every pmap_enter).
- Run 114 (boot sha1 648a3c7a): maps the same kernel text a second time write-back (TEX=001 C=1 B=1), read-only, and
  logs `S5WB wt-ticks wb-ticks wb-par hash`. If WB is far faster, the fix is moving RAM to write-back, which needs:
  PTE cleaning on every page-table write (pte_sync exists, leo_enter paths lack it) or WB table walks (TTBR IRGN/RGN),
  clean+invalidate when a page changes attributes or owner, consistent attributes for kernel/user aliases (the CA
  frame surface is mapped in backboardd and the kernel), and the existing exec-page D-clean/I-invalidate.

## Run 114 result and run 115 (flashed 2026-11-08, boot only)
- Run 114: no S5WB line: the ml_io_map test mapping was not created (va 0). S5PAR repeated run 113.
- Run 115 (boot sha1 9ddd49fb): private 512 KiB kmem_alloc buffer, its own PTEs switched to write-back, filled from
  kernel text and hashed vs the same 512 KiB of kernel text (WT). `S5WB stage va pte0 wt-ticks wb-ticks wb-par`
  (stage 3 = test ran; 1/2 = PTE lookup failed; 0 = allocation failed).

## Run 115 result and run 116 (flashed 2026-11-08, boot only)
- Run 115 S5WB: stage 3, write-back buffer PAR ...254 (inner WB-WA): 512 KiB hash WB 0xec7fa vs WT 0x116c73 ticks:
  write-back only ~15% faster, both ~350 CPU cycles per word. The cache mode is not the bottleneck: either caches do
  not serve hits, or the L2 is off / the memory bus is at a minimum clock (no RPM bandwidth/clock votes are made).
- Run 116 (boot sha1 0d04a387): S5CACHE line: 512 KiB of hashing as 8 KiB x 64 (fits L1) and 256 KiB x 2 (fits L2)
  from the WB buffer, and 8 KiB x 64 from WT kernel text. L1-sized fast -> caches work, bus/L2 slow; all slow -> caches
  not effective.

## Run 116 result and run 117 (flashed 2026-11-08, boot only)
- Run 116 S5CACHE (ticks per 512 KiB hashed, 19.2 MHz): 8 KiB x 64 from the WB buffer 0xea5cc, 256 KiB x 2 WB
  0x160b83, 8 KiB x 64 WT text 0xea564. Even L1-sized working sets cost ~366 CPU cycles per word: the data cache
  serves no hits, although SCTLR.C was set right after enabling (S5DCACHE).
- Run 117 (boot sha1 73ac5d2c): S5CTL line: live SCTLR and ACTLR at test time, PMU cycles for 1M loads from one
  64-byte line (pure L1 hits) and for the same empty -O0 loop.

## Run 117 result and run 118 (flashed 2026-11-08, boot only)
- Run 117 S5CTL: SCTLR 00c5587d (C still set at test time), ACTLR 0; 1M loads of one 64-byte WB line: 102M cycles vs
  6M for the empty loop (-O0, stack accesses forward from the store buffer): ~96 cycles per load, no L1 hit. The
  data cache is enabled but serves no hits. Mainline Linux needs nothing beyond SCTLR.C for Krait.
- Run 118 (boot sha1 a1e0e3b4, 10186752 bytes: read back count=2487, the flash check used 2486): S5PMU line with
  L1D access/refill and L2D access/refill events during 64 x 8 KiB hashing.

## Run 118 result and run 119 (flashed 2026-11-08, boot only)
- r118 verified in full on the phone (2487 blocks, a1e0e3b4).
- Run 118 S5PMU: 64 x 8 KiB hash = 131502 L1D accesses, 129 L1D refills, 923646 cycles (~7 cycles/word): the L1
  data cache WORKS (since run 111). The earlier "~366 cycles/word" figures came from assuming mach_absolute_time ticks
  at 19.2/24 MHz; the same work measured 960972 mach ticks in run 116, i.e. mach ticks look close to CPU cycles.
  Every ms figure from the profiler (runs 107-118) may be off by a large factor. Diagnosis of "uncached memory" is
  unproven; re-derive after run 119.
- Run 119 (boot sha1 dc719a71, 2487 blocks verified): S5CLK line: mach ticks, CNTPCT ticks, PMU cycles, PMCR and
  CNTFRQ around the same 8 MiB of hashing.

## Run 119 result: real units; run 120 (payload pl106, flashed 2026-11-08; boot stays r119 dc719a71)
- S5CLK: same work = 217,133,281 mach ticks, 4,168,988 CNTPCT ticks (CNTFRQ 19.2 MHz), 208,446,675 PMU cycles:
  mach_absolute_time counts NANOSECONDS; CPU 960 MHz. s5/tools/prof.py now uses ns. All earlier ms figures from
  runs 107-118 were ~42x too high; the cache/uncached diagnosis built on them was wrong (the D-cache enable and the
  panel mapping change are still correct and harmless).
- Corrected run 111 profile: kernel present + mirror ~7 ms/frame (fine). backboardd 50-85 ms/frame (12-20 fps),
  almost all in r_tri: ~30 triangles take 33-57 ms. The rect fast path never hits on the device (0 rects), although
  DRAWK shows plain 6-vertex quads (e.g. full-screen 480x800).
- pl106 (sha1 1cf71d92, 460 sectors): RREJ line after each PROF: counts per r_rect_impl reject reason 1..12
  (1 r_fast/iw==0, 2 iw differs, 3/4 more than two distinct x/y, 5 fewer than two, 6 same corner different
  attributes, 7 missing corner, 8 corner repeated in a triangle, 9 no shared diagonal, 10 colour not flat,
  11 texcoords not separable, 12 too big).

## Run 120 result and run 121 (payload pl107, flashed 2026-11-08; boot stays r119)
- RREJ: nearly all rejects are reason 10 (corner colours / c1.a differ), ~15 per frame; a few reason 11 (texcoords
  not separable). backboardd 46-96 ms/frame, 30-33 triangles, 0 rects.
- pl107 (sha1 3d64b9bf, 460 sectors): RREJC lines for the first 12 colour rejects: fmode<<16|prog, size, then per
  corner colour (RRGGBBAA x255) and c1.a x1000.

## Run 121 result and run 122 (payload pl108, flashed 2026-11-08; boot stays r119)
- Run 121: SpringBoard died of SIGPIPE (status 0xd) writing the logger's output to its stderr -> no home screen.
  pl108 sets F_SETNOSIGPIPE on stderr in SpringBoard too.
- RREJC: corner colours are identical; only c1.a differs (corner 0 = 0.125, others 0) in fmodes 0/1/2, where c1 is
  unused. pl108 compares c1.a only for R_FM_C1A, so CA's quads should take the rect fast path (watch PROF: rects > 0,
  tri time down). pl108 sha1 0c447fae.

## Run 122 result (2026-11-08)
- SpringBoard and backboardd stayed up (no SIGPIPE). User: home screen still not reached. Touches went to
  SBLockAlertWindow (a lock-screen alert window, cf. the earlier thermal "needs to cool down" alert); Home presses
  counted. Frame dump (last frame) is all black and the S5GUI centre sample is black: unknown what is shown.
- Renderer: idle frames 20 ms (50 fps), 15 draws / 30 tris, still 0 rects: rejects moved to reason 11 (texcoords
  not separable, ~15/frame).

## Run 122 retry result and run 123 (payload pl109, flashed 2026-11-08; boot stays r119)
- Retry unlocked fine (SBEVT SBAppWindow): the earlier failure was the usual unlock flakiness. SBLockAlertWindow is
  simply the lock screen's window.
- Home screen profile: idle ~100 ms between frames with ~1 ms drawing (nothing animating). Swiping pages ~110 ms per
  frame (9 fps): ~35 ms drawing 105 triangles (0 rects), ~73 ms outside GL (CoreAnimation / SpringBoard on one
  960 MHz core).
- pl109 (sha1 12e49fc1): r_rect compares t only for texture-sampling modes and t1 only for R_FM_MASK (reason 11 was
  the unused t1 set, like c1 in run 121). Bigger future levers: CPU clock (960 MHz of up to ~2.3 GHz; needs Krait
  PLL + voltage via RPM) and the other three CPU cores.

## Run 123 result and run 124 (flashed 2026-11-08: boot r124 + payload pl109)
- Rect fast path works on the device now: lock screen 14 rects + 2 tris per frame, drawing ~8 ms (was 35-45 ms).
- Unlock failed: SpringBoard logged only 3 of 29 touches. backboardd re-renders the lock-screen shimmer at ~48 fps
  (20.7 ms frames: ~8 ms GL + ~12 ms CA/present) and starves SpringBoard on the single 960 MHz core.
- Run 124 (boot sha1 e5bdadd7, 2487 blocks verified): IOS7LeoDisplay emulated vsync 33333 us on J1_S5
  (LEO_VSYNC_US; was 16667), i.e. CA renders at most ~30 fps.

## Run 124 result (2026-11-08): ~10 fps felt (was ~4), unlock works
- 30 Hz vsync cap works: lock screen 25 fps (40 ms frames: 8-9 ms GL, rest CA + present + waiting for vsync).
  Swiping home pages: 50-90 ms/frame (11-20 fps) with only 5-12 ms of GL (100+ draws, ~120 rects, ~45 tris).
  Idle home screen: 100 ms between frames, <1 ms GL.
- Remaining cost is outside GL: CoreAnimation/SpringBoard CPU on one 960 MHz core, the kernel present (~7 ms, of
  which ~5 ms is the row hash over the 1.5 MB frame), and background churn: in the ring window assetsd crashed and
  respawned 9 times and networkd 8 times (launchd restarts them every ~10 s), MobileGestaltHelper 3 times. The felt
  "slows down over time" matches frames growing from ~50 to 60-90 ms later in the run while GL time stays flat.
- Options: stop the crash loops (disable or fix assetsd / networkd: root HFS edit), raise the CPU clock (Krait HFPLL
  + voltage via RPM, risky), SMP bring-up (large), cheaper change detection in the kernel mirror.

## Run 125: CPU clock raise (flashed 2026-11-08; boot r125 ef28738c, 2487 blocks verified; payload pl109)
- s5_raise_cpu_clock() in PE_init_SocSupport_stub (pexpert/arm/pe_exynos3475_j1.c), from mainline Linux
  drivers/clk/qcom (hfpll.c / clk-hfpll.c / clk-krait.c / krait-cc.c): CPU0 HFPLL at 0xF908A000 (mode 0x00 bits
  OUTCTRL/BYPASSNL/RESET_N, L 0x04, user 0x10 VCO bit 20 above 1248 MHz, status 0x1c bit 16 lock); CPU0 mux = L2
  indirect reg 0x4501 via mcr/mrc p15,3,...,c15,c0,6/7 (primary [1:0]: 1 HFPLL, 0 secondary; secondary [3:2]: 2 =
  acpu_aux; LPL copies 8 bits up). Parks on acpu_aux, reprograms L to J1_S5_CPU_L (78 = 1497.6 MHz), waits for lock,
  switches back; restores the old L if no lock. Only acts if PLL on, L in 30..70, mux on HFPLL. Voltage untouched.
- Log: S5DCACHE line "clk mux0,mode,L,cyc0,status,result,mux1,L1" (result 600dxxxx = locked, badxxxxx = no lock,
  e1/e2 = skipped) followed by the loop measurement at the new clock (cycles/ticks*19.2 = MHz).
- Fallback if it fails to boot: r124 e5bdadd7. Disable: J1_KFLAGS=-DJ1_S5_CPU_L=0.

## Run 125 result and run 126 (flashed 2026-11-08)
- Run 125 (CPU clock raise to L=78): kernel panic at boot (user report). The ring on eMMC was from the previous boot
  (panic before any dump); TWRP has no /dev/mem, so the RAM ring cannot be read. Cause unknown: the CP15 L2 indirect
  access (mcr/mrc p15,3,..,c15,c0,6/7), the HFPLL mapping/read at 0xF908A000, or the switch itself.
- Run 126 (boot sha1 a9105c61, built with J1_KFLAGS=-DJ1_S5_CPU_L=0): read-only probe: reads mux 0x4501 and the
  HFPLL registers, writes nothing; result slot = 0xd0. If it panics, the read path faults (need a photo of the panic
  screen). Fallback: r124 e5bdadd7.

## Run 126 result and run 127 (flashed 2026-11-08)
- Run 126 (read-only clock probe) panicked too, at the same early point as run 125 (user photo): "pmap_static_init:
  Bootstrapping pmap" then "Fatal exception: sleh_abort: kernel map is NULL, probably a fault before vm_bootstrap?",
  far 0xc063b000, fsr 0x7 (translation fault), pc 0x80405e5a, lr 0x800d8e09 (in pmap_map_bd). This is before
  PE_init_SocSupport, i.e. before any clock code runs: the boot-time io_map chain in pmap.c (panel FB 0x5ef000 bytes,
  ring, GIC, timer, crumb, MDP, WDT, PS_HOLD ... reaching ~0xc063b000) faults depending on kernel layout. r124 booted
  with the same kernel byte size, so the trigger is layout, not size. LATENT BUG: investigate pmap_map_bd /
  leo_boot_bd_replace for io_map ranges crossing into section 0xc06xxxxx.
- Run 127 (boot sha1 00f75d04, 2486 blocks verified): diagnostic block (S5PAR/S5WB/S5CACHE/S5CTL/S5PMU/S5CLK)
  removed from ios7j1_display_present (copy in scratchpad pe_before_strip.c); CPU clock raise to L=78 enabled.
  Fallback r124 e5bdadd7.

## Run 127 result and run 128 (flashed 2026-11-08)
- Run 127 panicked the same way; with matching symbols the pc is PE_init_SocSupport_stub+0x85e = the first HFPLL
  read (pll[0]) in s5_raise_cpu_clock: PE_init_SocSupport runs before vm_bootstrap ("kernel map is NULL") and
  ml_io_map there returned an unmapped VA (far 0xc063b000). Runs 125/126 were the same fault (not a layout bug; the
  earlier note blaming layout is wrong).
- Run 128 (boot sha1 df2f8f49, 2487 blocks verified): clock raise moved to IOS7S5Touch.cpp ios7s5_cpu_clock(), run
  once from the first ios7s5_keys_poll (backboardd has opened the touch service), HFPLL mapped with
  IOMemoryDescriptor::withPhysicalAddress. IOLog lines "S5CLK: before ... MHz" and "S5CLK: locked/NO LOCK ... now N
  MHz". Target L=78 (1497.6 MHz), voltage untouched. Fallback r124 e5bdadd7.

## Run 128 result (2026-11-08): CPU at 1497 MHz, "much faster, ~20 fps"
- S5CLK: before mux 00230001 mode 01000007 L 50 user 00000009 status 00010001 960 MHz; locked after 2 us, mux
  00230909 L 78, now 1497 MHz. Stable through the run at LK's core voltage.
- Frames: swiping ~55 ms (18 fps); drawing 2-9 ms; the rest is CA/SpringBoard CPU + the 30 Hz vsync cap.
- Next levers: higher L (1.7-2.0 GHz; may need the core voltage raised: krait regulator/PMIC, not done), stop the
  assetsd/networkd crash loops, raise the vsync cap back toward 60 Hz now that the CPU has headroom (watch unlock),
  cheaper kernel mirror change detection (~5 ms/frame row hash), SMP.

## Multi-core, stage 1: run 129 (flashed 2026-11-08; boot r129 370b585f, payload pl109; clock 1497 MHz kept)
- XNU on this port is strictly UP (cpu_number() = 0, cpu_start() no-op, no IPIs, TLB shootdown TODO). Full SMP is a
  multi-week project. Plan: helper cores outside XNU: CPU1-3 run our own loop taking jobs from shared memory
  (stage 2: coherent memory (ACTLR.SMP + shareable attributes) and the kernel present/mirror on CPU1; stage 3:
  split backboardd rendering across CPUs 1-3).
- Stage 1 (IOS7S5Touch.cpp ios7s5_wake_cpu1, from the first keys poll after the clock raise): legacy atomic SCM
  set_boot_addr (smc #0, r0 0x401222, r2 0x01 = CPU1 cold, r3 = kvtophys(s5_cpu1_entry)), kpssv2 power-up of ACC1
  0xF9098000 (+0x14 0x403f0001/0x403f007f/0x403f3f7f, L2 SAW 0xF9012000+0x1c = 0x10003, +0x04 0x21/0x20/0/0x80),
  SGI 1 to CPU1. s5_cpu1_entry (ARM, MMU/caches off) writes 'S5C1' + MPIDR to s5_cpu1_mailbox and increments word 1
  forever. Logs: "S5SMP: entry pa", "SCM set_boot_addr ret", "released, mailbox magic ... count ... mpidr",
  "CPU1 heartbeat N" every ~30 s. Sources: Linux v6.1 platsmp.c, qcom_scm.c, qcom_scm-legacy.c, qcom-msm8974.dtsi.
- Disable: J1_KFLAGS=-DJ1_S5_SMP=0. Fallback r128 df2f8f49.

## Run 129 result (2026-11-08): CPU1 runs our code
- S5SMP: entry pa 2047da80, SCM set_boot_addr ret 0, released, mailbox magic 53354331 count 1 mpidr 80000001;
  heartbeat 13114 / 26319 / 39523 / 52709 at ~30 s intervals (~440 counts/s of a 1M-iteration loop: CPU1 at ~0.9 GHz,
  its own HFPLL untouched). System otherwise normal.
- Payoff check: swipe frame ~55 ms = present ~7 ms + GL 2-9 ms + CA/SpringBoard ~40-45 ms. Helper cores (our own
  loop, outside XNU) can only take present + GL: at best ~15-25%. The rest needs XNU to schedule threads on CPU1-3.
- Stage 2 design (if pursued): CPU1 with MMU on: TTBR1 = kernel TTBR1 (live kernel L1, TTBCR N=2), TTBR0 = private
  table with an identity section for the trampoline, same SCTLR/DACR; own VBAR fault catcher; TLBIALL + DCIMVAC over
  the frame before each job (producer pages are write-through, so no shareability needed); mailbox job = mirror
  present (row hash, expand, panel write, MDP kick) asynchronously.

## Run 130 result and run 131 (flashed 2026-11-08)
- Run 130: CPU0 at 1728 MHz (L=90): locked after 3 us, self-test (fixed integer/memory workload, 8 runs vs the
  960 MHz reference) 0 of 8 wrong; ran the whole session without a crash. CPU1 heartbeat as in run 129.
- Run 131 (boot sha1 13221318, 10235904 bytes): helper core stage 2. pexpert/arm/s5_helper.c (-O2): CPU1 boots into
  s5_tramp (__DATA,__s5tramp, physical entry) which invalidates its L1/TLB, loads DACR/TTBCR/TTBR0 (private one-page
  table with an identity section for the trampoline)/TTBR1 (kernel live table)/VBAR (s5_vectab fault recorder) from
  s5_tramp_box, turns on CPU0's SCTLR and jumps to s5_helper_main (own 16 KiB stack). Jobs: CPU0's present calls
  s5_present_mirror(): posts {px,row,seq} to the box (drops a frame if CPU1 is still busy) or mirrors locally when the
  helper is not active / dead (fault state 9, or 1 s without progress). CPU1: TLBIALL + DCIMVAC over the frame, then
  s5_mirror_frame (row hash / expand / panel write / MDP kick, 50 ms kick spacing). Logs: "S5SMP: released: helper
  state ..., enable N" and periodic "S5SMP: CPU1 state .. jobs .. (posted ..) rows .. busy .. ms fault ..".

## Run 131 result and run 132 (built 2026-11-08; an earlier r132 2c5e8aca without the cache clean got flashed first)
- Run 131: helper core reached C (state 2, mpidr 80000001, no fault, enable 1); 12 jobs, 0 changed rows: CPU1 saw
  the previous frame every time. User photos: stale mixes of old frames, lock screen pieces missing, the boot test
  pattern left on screen. FPS felt good (CPU0 freed).
- Causes: (1) frames were dropped while CPU1 was busy, so the last frame of an animation could stay unmirrored;
  (2) the frame surface is mapped write-back (kIOMapCopybackCache): backboardd's pixels sit dirty in CPU0's cache.
- Run 132 (boot sha1 7070b495): CPU0 always posts the newest frame (no drop; CPU1 re-mirrors when the sequence moved)
  and cleans the frame to RAM (DCCMVAC over row*800 bytes) before posting. Dead detection: no B_DONE progress for 1 s.

## Keyboard (reported 2026-11-08): never renders
- The iPad2,2 dyld cache contains only /System/Library/PrivateFrameworks/TextInput.framework/KBLayouts_iPad.dylib;
  TextInput loads "KBLayouts_%@.dylib" for the device family (strings KBLayouts_iPhone/iPod/iPad.dylib present), and
  this device reports itself as an iPod (status bar "iPod"), so no layout library loads. Options: (a) make the layout
  lookup use "iPad" (shim hook in apps; iPad layouts are sized for 768/1024-wide screens, may need scaling), or
  (b) supply the iPhone/iPod layouts from an iPhone/iPod 7.1.2 IPSW kept local (Apple binary: never published).
- GitHub repo for the S5 build: github.com/Quantitative101/iOS7-KLTE-GalaxyS5 (user created it empty).

## Run 132 result (2026-11-08): helper core mirrors correctly
- CPU1: jobs == posted (1942/1942), 32379 rows rewritten, ~6.9 ms busy per job, no fault. Rendering correct.
  CPU0: lock screen steady 25 fps (40 ms), idle home ~10 fps, swipes 13-21 fps.
- "Slows down after ~1 min": no growth in frame cost in the ring window, free memory steady (~66k pages), no jetsam;
  assetsd crash-respawns every ~10 s (11 in the window). Plan: in the same root HFS edit as the keyboard layouts,
  either create the Photos library store or disable com.apple.assetsd's relaunch.

## 2026-11-08 later: S4 Mini port done, repos, keyboard source
- S4 Mini: subagent ported the applicable fixes (sandbox ext, entitlements, WCOMB->WT, normal-NC panel mapping, 30 Hz
  vsync, data cache on, -O2 mirror loops); s5_helper.c wrapped in #if J1_S5 (it broke S4/Tab2 links). Built
  ports/build/s4mini/recovery.xnu.img b86d5a83 + j1gl.payload 72007233; not flashed (needs approval). Details in
  memory project-s4mini-userland.md.
- Repos: a subagent is updating github.com/Quantitative101/iOS-Note8-Great (published patch predates the v121-v130
  clock/thermal-guard work) and creating github.com/Quantitative101/iOS7-KLTE-GalaxyS5 (default clock 1.5 GHz).
- Keyboard: downloaded ports/ipsw/ipod5_1_7.1.2/iPod5,1_7.1.2_11D257_Restore.ipsw (sha1 f935397e, matches Apple's);
  rootfs 058-4385-009.dmg key from `ipsw download keys --device iPod5,1 --build 11D257`; decrypting with
  ports/tools/vfdecrypt.py to ex/rootfs.dmg. Next: extract KBLayouts_iPod.dylib from its dyld cache, install it on
  the S5 root at /System/Library/PrivateFrameworks/TextInput.framework/, plus stop the assetsd crash loop.
- iPod5,1 rootfs decrypted (ports/ipsw/ipod5_1_7.1.2/ex/rootfs.dmg, volume Sochi11D257.N78OS). Its cache holds
  KBLayouts_iPhone.dylib (iPods use the iPhone layouts): __TEXT 0x34a3b000 +3.6 MB (3.4 MB code), __DATA 0x39bda000;
  links CoreGraphics/UIKit/Foundation/libSystem/CoreFoundation/libobjc. Neither Xcode's dsc_extractor.bundle (mmap
  EINVAL: format too old) nor `ipsw dyld` (parse EOF) can extract it. A standalone copy needs a custom decache (text
  -> data movw/movt distances, cross-image BLs, objc/cfstring binds): large. Alternatives: iPad layouts fallback, or
  move the whole userland to the iPod5,1 root (phone idiom natively; shim's hard-coded SpringBoard/backboardd offsets
  must be re-derived).
