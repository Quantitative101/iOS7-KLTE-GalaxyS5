# Galaxy S5 port: development log

The raw working notes kept during the port, newest sections at the end. Paths such as `s5/build/...`, `ports/build/disk.img`
or `ports/ipsw/...` refer to the developer's local tree: boot images, payloads, root filesystem images, logs and
anything from Apple's or Samsung's firmware are not part of this repository. Run numbers count test boots on the phone.

# S5 next session (written overnight 2026-11-07/08, phone offline)

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

## Run 133: iPod5,1 userland (2026-11-08)
- Pristine SpringBoard and backboardd are byte-identical on the iPad2,2 and iPod5,1 roots (same build 11D257), so our
  patched copies move over as-is. Root changes vs pristine iPad (rsync diff): ringtones + UISounds/New removed,
  LaunchDaemons cut to 48 (rest deleted), fairplayd.A1 = fbtest + plist, j1shim.dylib added, SpringBoard (bx lr
  patches) + backboardd (LC_LOAD_DYLIB) patched, dyld cache patched in 3 places: IOKit "powerd" stubs (bx lr) at
  iPad 0x2e18d780/0x2e18d908 = iPod 0x2e18e780/0x2e18e908 (found by masked function match), libMobileGestalt
  mov #-1 -> #0 at iPad file 0xb6bd9fe = iPod file 0xb2159fe (unique context match).
- s5/tools/build-ipod-root.sh builds ports/ipsw/ipod5_1_7.1.2/ex/root-ipod.hfs (1790 MiB HFSX, sha1 4704f820): kept
  daemons = the phone's 48 (jetsamproperties.K94 -> N78; others parked in LaunchDaemons.disabled).
- Layout: the iPod root does not fit the old 1.41 GiB root slot. Written at userdata sector 4456448 (piece 17,
  BASE=17 bash s5/tools/write-userdata.sh ...), 3670016 sectors; MBR entry 1 repointed (s5/build/userdata-mbr.ipod.bin
  818996f5). Old MBR backup s5/build/userdata-mbr.before-ipod.bin (44d82ef4) = the iPad root at sector 2048, still
  intact: restore that sector to go back. Kernel unchanged (rd=disk0s1 via the FDisk scheme).
- Root write progress: pieces 0-8 of 14 verified (BASE=17). TWRP reset itself twice ~6-7 min into long adb sessions
  (no user action, battery 100%): likely the APPS watchdog the stub arms is never petted by TWRP's kernel after an iOS
  run resets into recovery. Resume: BASE=17 bash s5/tools/write-userdata.sh .../root-ipod.hfs 9. Follow-up: disarm
  the watchdog on the recovery-reset path (or have the stub not arm it when booting recovery).
- 2026-11-08: all 14 pieces of root-ipod.hfs written + verified at userdata sector 4456448; MBR switched
  (818996f5 read back). First iPod-userland boot (run 133: kernel r132 7070b495, payload pl109). Revert: write
  s5/build/userdata-mbr.before-ipod.bin to userdata sector 0.
- Run 133 result: "gl missing" in every process; fairplayd.A1 (fbtest, copies the payload to /var/tmp/j1gl.dylib) never
  launched: build-ipod-root.sh had cp'd its plist (uid 501), launchd skips non-root plists. Fixed on device by
  renaming a parked root-owned plist into place and overwriting it (owners-off mount), 24 sector runs written with the
  new s5/tools/write-sector-runs.sh (base sector 4456448). root-ipod.hfs = fixed image (v1 kept as root-ipod.v1.hfs);
  build script fixed. Run 134 = same boot/payload, fixed root.
- Run 134: fairplayd.A1 still never launched on the iPod root (its job is fairplayd.O2; the A1 binary was also
  uid 501). Fixed: com.apple.fairplayd.O2.plist moved back from LaunchDaemons.disabled (root-owned) and rewritten
  (RunAtLoad/KeepAlive, /usr/sbin/fairplayd.O2), fbtest cat'd over /usr/sbin/fairplayd.O2 (chmod u+w, then 555;
  stays root-owned), A1 plist parked. 30 runs written. root-ipod.hfs = this image (v2 kept). TODO: fold into
  build-ipod-root.sh (keep O2 instead of A1).
- Run 135 (iPod userland with fairplayd.O2 fix): WORKS: lock/home screen, Setup skipped, iPhone keyboard appears
  (user photo). Issues: keyboard uses the 320-pt portrait layout (2/3 of the 480-pt screen); keyboard backdrop
  (UIBackdropView blur) not drawn so keys float over the home screen; icon labels missing on the home screen; several
  template icons remain (Settings, Calendar, FaceTime, Reminders, Game Center...).
- Run 135 log: SpringBoard died once of SIGPIPE (0xd): on the iPod root its stderr is a socket; F_SETNOSIGPIPE does
  not cover sockets -> also setsockopt(2, SOL_SOCKET, SO_NOSIGPIPE). No ICONFB/ICONMISS for app icons: the iPod root is
  retina-only (icons/artwork mostly @2x only), we run scale 1 (480x800) -> template icons, 320-pt keyboard.
  Proposal: present the S5 as a 640x1136 @2x (320x568 pt, iPhone 5 class) display: kernel framebuffer/surface size,
  MG main-screen-* (via /chosen display-scale=2 and the framebuffer size), touch mapping, mirror 640->1080 (x1.6875,
  1917 rows), shim screen-geometry fallbacks. ~1.9x pixels per frame.

## Run 136: 640x1136 @2x display (flashed 2026-11-08: boot r136 78cb13f5, payload pl110 d8d3bbfd; iPod root)
- IOS7LeoDisplay.cpp (J1_S5): leoWidth/Height/Row 640/1136/2560, offscreenPixelBytes 2908160, S4-family fullscreen
  surface answer = that geometry, display info[6]/[7] = 640x1136 (the 480x800 J1 console buffer is unchanged).
- s5_helper.c mirror: 640x1136 -> 1080x1917 (x27/16 both axes, panel rows 1..1917), row hash over 640 words,
  clean/invalidate range row*1136.
- Touch: panel -> GUI x16/27 (y offset 1), clamp 639/1135 (IOS7S5Touch.cpp); wire normalisation /639, /1135
  (IOS7LeoTouchWire.h).
- Device tree /chosen display-scale=2 (s5/stub/build.sh). bsd/vfs/vfs_syscalls.c open_nocancel: read-only opens of
  /private/var/mobile/Library/Caches/com.apple.MobileGestalt.plist return ENOENT so MG recomputes main-screen-*.
- Shim pl110: UIScreen fallbacks 320x568 / 0,20,320x548 and scale 2.0; stderr also gets SO_NOSIGPIPE (SpringBoard
  died of SIGPIPE in run 135: its stderr is a socket on the iPod root).
- Expect ~1.9x pixels per frame. Fallback: r132 7070b495 + pl109 (480x800).
- Run 136 result: "glitched block in the centre". Surfaces were 640x1136 (ATTACH 280x470 row a00) and MG scale 2,
  but MG main-screen-width/height still 480x800: libMobileGestalt uses IOMobileFramebufferGetDisplaySize, answered
  by IOS7LeoDisplay selector 8 from PE_state.video (480x800). backboardd then died twice (prefetch abort into heap
  0x15911400 from QuartzCore under an IOMobileFramebuffer callback) - CA display state sized from 480x800 vs 640x1136
  surfaces. iPod cache: ___pthread_kill unslid 0x381ae1e8 (slide = pc - 0x381ae1f0).
- Run 137 (boot sha1 6933f387, payload pl110): selector 8 GetDisplaySize and the swap-metadata expected[13/14/29/30/
  53/54] use leoWidth/leoHeight (640x1136 on S5; equal to the console size on other boards).
- Run 137 result: MG now 640x1136 @2x (9c400/115580/7d0), backboardd stable, presents fine; SpringBoard crash-loops
  on a NULL deref in CoreImage's GL path (UIKit <- SpringBoardFoundation -> CoreImage; same crash as Notes in runs
  102-106), no GL in SpringBoard.
- Run 138 (payload pl111 9bb98325, boot stays r137): SpringBoard also gets app_coreimage_software() (CPU CIContext)
  and app_no_eagl() (EAGLContext init -> nil).
- Run 138 result: CIFIX + EAGLNIL fired in SpringBoard, still the same CoreImage NULL-GL crash loop (CoreImage uses
  the current EAGL context's GL table regardless). Run 139 (payload pl112 e1d1db8d, boot r137): SpringBoard falls
  through to the EAGL hooks (fake macro context + software GL, like backboardd) via `goto eagl_hooks`; CPU CIContext
  kept.
- Run 139 result: SpringBoard with software EAGL/GL stays up after one crash (QuartzCore recursive layer walk NULL
  deref, as Settings in run 104/105), backboardd composites full 640x1136 frames but the only content is a 129x158
  texture at (255,451) (the "block"); SpringBoard spends its time in CoreImage at 744x1392 through the software GL
  and never draws its UI. Evidence is growing that the shim GL inside client processes corrupts QuartzCore state.
  Recommendation: return to 480x800 @1x on the iPod root (worked in run 135) and add @2x->@1x image fallback and a
  keyboard width fix instead.

## Run 140-141: back to 480x800 @1x (2026-11-08)
- Run 140 (boot r140 dd7f4db6, payload pl113 2bb638dc): 2x geometry gated by J1_S5_GUI2X (kernel) / J1_GUI2X (shim),
  both default 0; display-scale=1; SpringBoard back to no GL. Result: black screen. MobileGestaltHelper crash loop and
  SpringBoard dying in CoreImage's GL path (same NULL-GL crash as runs 137/138).
- Cause: the run 136 MobileGestalt-cache deny (vfs_syscalls.c) was still active. The /var cache still holds the run 135
  answers (480x800, scale 1.0, iPod); run 135 read it and worked.
- Run 141 (boot r141 948a0b17, payload pl113): the cache deny is also under J1_S5_GUI2X (off).
- Run 141 result: black again. MobileGestaltHelper no longer crashes (MG reads the cache: 480x800 @1), but SpringBoard
  still dies in CoreImage GL. /var/mobile/Library/SpringBoard/LockBackground.cpbitmap was 744x1392 (trailer
  0x2e8/0x570): the wallpaper saved at 640x1136 in runs 137-139, which SpringBoard reprocesses through CoreImage.
- Run 142 (same boot r141 + pl113): LockBackground.cpbitmap and LockBackgroundThumbnail.jpg deleted from /var
  (27 sectors in 21 runs via write-sector-runs.sh, base 2959360; before/after images var141*.img in the scratchpad).
- Run 142 result: back to normal (home screen at 480x800 @1x). Settings crashed, but the ring on disk ends at ~61 s:
  dumpLog IOMalloc'd its 260 KB buffer per dump and silently skipped dumps once that failed (free pages 116823 ->
  68827 by dump 2). TWRP note: busybox dd `skip=` reads through the skipped data (~12 GB for the ring, minutes; looks
  like a hang). Read the ring through losetup -o 12679888384 instead.
- Run 143 (boot r143 7db522ef, payload pl113): dumpLog buffer allocated once (static), failure logged.
- Run 143 result: Settings opened fine (no crash); ring ran to ~100 s (user rebooted).

## Run 144: @1x artwork and keyboard width (2026-11-08)
- s5/tools/make-1x-images.py: decodes Apple CgBI PNGs (raw deflate, BGRA premultiplied), writes half-size standard
  RGBA PNGs for every @2x PNG without a 1x sibling. 622 made for /Applications and SpringBoard.app (4.6 MB).
  Copied onto root-ipod.hfs (old image kept as root-ipod.v3.hfs); 14459 sectors in 296 runs, device pre-checked
  equal to v3 in those runs, written + verified at base 4456448.
- Shim pl114 (40f10d0b, seq 6aef7bde): fix_keyboard_width() in apps and SpringBoard hooks the class methods
  defaultSizeForInterfaceOrientation: / sizeForInterfaceOrientation: / keyboardSizeForInterfaceOrientation: /
  _defaultSizeForInterfaceOrientation: / defaultSizeForOrientation: on UIKeyboard, UIKeyboardImpl, UIPeripheralHost,
  UIKeyboardLayout, UIKeyboardLayoutStar, UIInputViewSet (whichever exist) and widens portrait answers < 480 to 480.
  Log tags: "KBWIDE hook" (installed), KBWIDE (slot, orientation, width before, height).
- Boot stays r143.
- Run 144 result: icons fixed except Weather and Calculator (their new icon.png files are fine); keyboard now spans
  the screen, stretched.
- Run 145 (same boot r143 + pl114): /var/mobile/Library/Caches/com.apple.IconsCache emptied (SpringBoard's cached
  icon images from the template-icon runs; 84 sectors in 24 runs at base 2959360). /var copy pulled via losetup -o
  1515192320 (fast) instead of dd skip.
- Run 145 result: all icons fine. Keyboard stretched to 480 (OK) but too high and shifted right. KBWIDE: hooks on
  UIKeyboard/UIKeyboardImpl (defaultSize..., sizeFor..., keyboardSizeFor..., _defaultSize...); portrait 320x216
  widened to 480x216; landscape 800x162 / 568x162 untouched. SpringBoard died twice of SIGPIPE (0xd) at boot (Mail
  0xf with it). Home screen shot: icons right, no labels, black wallpaper.
- Run 146 (boot r143, payload pl115 9d2513bc seq 6aef7bdf): own setFrame: of UIKeyboard / UIPeripheralHostView /
  UIKeyboardAutomatic / UIKeyboardLayoutStar / UIKBKeyplaneView / UIInputSetHostView hooked: frames >= 480 wide pinned
  to x=0, first 60 logged as KBFRAME (slot x y w h). SpringBoard: signal(SIGPIPE, SIG_IGN).
- Run 146 result: same keyboard placement (tested on SpringBoard search). KBFRAME: only UIKeyboard has its own
  setFrame:, called with 0,0 480x216 (correct inside its host). SpringBoard no SIGPIPE deaths this run.
- Run 147 (boot r143, payload pl116 99135049 seq 6aef7be0): UIView setFrame:/setCenter: hooked with a class-name filter
  (UIPeripheralHost* clamp + log; UIKeyboard*/UIKB*/UIInput* log only) as KBHOST F/C lines: host strips 480 wide and
  < 400 high pinned to x=0 (centre x 240) and y >= 800-h.
- Run 147 result (KBHOST log): host UIPeripheralHostView 480x216 at y=584 (correct), but UIKeyboardImpl /
  UIKeyboardLayoutStar / UIKBKeyplaneView switch to 568x162: the 480-wide portrait answer made UIKit lay out the
  iPhone 5 LANDSCAPE keyplane (keys to x=568, 54 pt short) = "too high and right".
- Run 148 (boot r143, payload pl117 c22e0948 seq 6aef7be1): every hooked keyboard-size answer is 480x162 (3.5-inch
  landscape keyboard), landscape included, so the 480-wide landscape keyplane is used and the host is 480x162.
- Run 148 result: keyboard vertically correct, still offset to the right.
- Run 149 (boot r143, payload pl118 5c5843fa seq 6aef7be2, built, not yet flashed): isWideScreen (class and instance,
  UIKeyboardImpl / UIKeyboardLayoutStar / UIKeyboard / UIKBTree / UIKBRenderFactory / UIKeyboardLayout /
  UIPeripheralHost, wherever defined) answers NO so the 480-wide (3.5-inch) landscape keyplanes are chosen instead of
  the 568-wide ones. Log tag KBNARROW.
- Run 149 result: same (offset right). KBNARROW: no isWideScreen in the guessed classes. Run 148 log: host
  480x162 at y=638 (correct) but UIKeyboardLayoutStar / UIKeyboardImpl / UIKBKeyplaneView resize to 568x162.
- Run 150 (boot r143, payload pl119 08c2226e seq 6aef7be3): isWideScreen search over every class (objc_copyClassList,
  own methods, class + instance) -> NO, logged KBNARROW +/-Class. Fallback: UIKeyboardLayoutStar frames wider than 480
  (and < 400 high) get a horizontal squeeze transform 480/w, tx -(w-480)/2 (left-aligned); transform cleared before
  each setFrame. KBHOST S lines log squeezes.
- Run 150 result: keyboard fits visually (squeeze path: KBHOST S on UIKeyboardLayoutStar 568x162; the class walk only
  found -[IMAVCamera isWideScreen], unrelated). Tapping a key crashed SpringBoard: popup UIKBBlurredKeyView, data abort
  past a buffer end in AppSupport +0x223f6 (CPBitmap; iPod cache slide 0x325b000 this boot; found by matching the
  J1CRASH code bytes in the cache, scratchpad findcode.py) = stale key images in
  /var/mobile/Library/Caches/com.apple.keyboards/images (41 files from the 2x / 320-wide runs).
- Run 151 (boot r143, payload pl120 2bfc6ec3 seq 6aef7be4): isWideScreen walk no longer called; keyboard image cache
  emptied on /var (76 sectors, 23 runs).
- Run 151 result: one letter typed, then the same SpringBoard crash (AppSupport +0x223f6 reading past a CPBitmap,
  slide 0x29fc000 this boot) in the key popup: not stale files; the freshly stored keyboard-cache image is read past
  its end.
- Run 152 (boot r143, payload pl121 c860b4f7 seq 6aef7be5): UIKeyboardCache off: +enabled -> NO if present,
  -cachedImageForKey:fromLayout: -> nil; its methods logged as KBCACHE.
- Run 152 result: typing works (no crash), laggy/hangs (keyboard image cache off, every key redrawn).
- Run 153 (boot r143, payload pl121; iPod root edit): black wallpaper cause = DefaultWallpapers.plist is keyed
  ProductType -> DeviceColor -> DeviceEnclosureColor and MG says ProductType "QEMU-virt" (h9jDsbgj7xIVeIQ8S3/X3Q),
  DeviceColor / DeviceEnclosureColor "unknown" (mumHZHMLEfAuTkkd28fHlQ / JhEU414EIaDvAz8ki5DSqw; MG key hash =
  base64(md5("MGCopyAnswer"+key))[:22]). Added QEMU-virt -> unknown -> unknown -> default = static
  /Library/Wallpaper/iPod/304@2x~ipod.png (425 sectors, 11 runs, device pre-checked). root-ipod.hfs = this image,
  previous kept as root-ipod.v4.hfs (v1 deleted for space). Icon labels may depend on wallpaper legibility settings.
- Run 153 result: black screen. SpringBoard crash loop, CoreImage +0x1f4d8 NULL GL table (slide 0x3621000) <-
  UIImage drawInRect <- -[UIImage _imageWithBrightnessModifiedForLegibilityStyle:] <-
  -[SBFStaticWallpaperView _newTreatedImage] <- initWithFrame:wallpaperImage:treatWallpaper: (744x1392 wallpaper).
  Symbolised with scratchpad findcode.py (slide from crash code bytes), whichimg.py, objcsym.py (ObjC method tables
  from the armv7 cache).
- Run 154 (boot r143, payload pl122 0df90a1f seq 6aef7be6): SpringBoard forces treatWallpaper:NO in
  -[SBFStaticWallpaperView initWithFrame:wallpaperImage:treatWallpaper:] (log WALLFIX).
- Run 154 result: wallpaper (304) shows, SpringBoard stable.
- Run 155 (boot r143, payload pl122): root: QEMU-virt default wallpaper -> /Library/Wallpaper/iPod/101@2x~ipod.png
  (iOS 7 default), and 24 @1x images for MobileIcons.framework incl. AppIconMask~iphone.png (icons were unmasked =
  square at scale 1). 711 sectors in 45 runs, device pre-checked; root-ipod.hfs = this, previous = v5 (v2 deleted).
  /var: LockBackground.cpbitmap + thumbnail, IconsCache, sharedimagecache/Wallpaper emptied (81 sectors, 26 runs).
- Run 156 (boot r156 0e698bd0, payload pl122): J1_S5_FILL (default 1) in s5_helper.c and IOS7S5Touch.cpp: GUI rows
  x2.4 to panel rows 0..1919 (ROW0(y) = y*12/5), touch y = panel y * 5/12; no more 60-row bars (~6.7% vertical
  stretch). J1_S5_FILL=0 restores the letterbox. Run 155 result not reported before this flash.
- Run 156 result: SpringBoard stable, Safari opens (favourites grid, last frame).
- Run 157 (boot r156, payload pl123 26de4a22 seq 6aef7be7, J1_APPTEST=1): SpringBoard thread waits 60 s, then
  launches the 24 visible apps one by one (SBSLaunchApplicationWithIdentifier, 15 s each) and logs
  "APPTEST idx rc pid@3s pid@15s bundle-id", then "APPTEST done". Build the normal payload without J1_APPTEST after.
- Run 157 result (last 10 apps; the ring's circular half wrapped, first 14 lost): alive at 15 s: MobileSMS, FaceTime,
  Videos, VoiceMemos, Passbook, AppStore, MobileStore, Music (Music later killed 0x9). Game Center aborted: QuartzCore
  x-alloc.c:567 slab_dealloc assertion (as Settings in runs 104/105). Nike rc=8 (not launchable). assetsd crash loop =
  most of the ring: PhotoData exists, no Photos.sqlite; URL "...Photos.sqlite?readonly_shm=1" -> Cocoa error 260.
- Run 158 (boot r156, payload pl124 72e67448 seq 6aef7be8, J1_APPTEST=1 J1_APPTEST_COUNT=14): assetsd adds the store with
  the plain file URL (retry with nil options); first 14 apps re-tested.
- Run 158 result: PSTORE added (Photos.sqlite created), but assetsd still aborts: "attempt to write a readonly database"
  (store opened with its read-only options), loop still wraps the ring (only Maps, Safari, Mail results survive: all
  alive at 15 s). Last frame = Mail's Add Account list with no provider logos (@2x-only framework images).
- Run 159 (boot r156, payload pl125 75e6059a seq 6aef7be9, J1_APPTEST_COUNT=11): assetsd adds the store with nil options
  (original options logged as PSTORE-OPT). Root: 1700 @1x images for System/Library/{Frameworks,PrivateFrameworks,
  PreferenceBundles,CoreServices,AccessibilityBundles} via make-1x-images.py (77 merged runs, 38572 sectors, device
  pre-checked; root-ipod.hfs = this, previous v6, v3 deleted).
- Run 159 result (no ring loss of APPTEST lines; assetsd loop fixed, store options were read-only + WAL): alive at 15 s:
  Settings, Photos, Camera, Contacts, Calendar, Clock, Calculator, Weather. Crash: Notes + Reminders (CoreImage NULL
  GL table <- +[CUIShapeEffectStack sharedCIContext] <- CUITextEffectStack / UIImage _imageWithStylePresets), Stocks
  (QuartzCore +0xdcd8 NULL, recursive walk). networkd aborts repeatedly (0x6).
- Run 160 (boot r156, payload pl126 f04531c5 seq 6aef7bea, J1_APPTEST_MASK=0x110443: Settings, Notes, Reminders, Stocks,
  Music, Game Center): CIFIX key bug: ci_software_options set "kCIContextUseSoftwareRenderer" (the constant's name); the
  key is "software_renderer".
- Run 160 result: only "APPTEST start", Settings launched, ring ends right after the n=6 s4_dump_threads dump (~93 s),
  as runs 142/143. Cause not proven: s4_dump_threads walks all threads without locks and is the common point.
- Run 161 (boot r161 94e65cd0, payload pl126): s5LogThread no longer calls s4_dump_threads. Same 6-app test.
- Run 161 result: same early stop (last ring dump ~93-108 s, only "APPTEST start"), so s4_dump_threads was NOT the cause.
  CIFIX never fired (Settings only), so the CoreImage key fix is still untested. Touch at ~84 s on SBLockAlertWindow: the
  phone was still locked when the test launched Settings at ~60 s; SBSLaunch may block behind the lock. Backdrop shaders
  and UDA LIVELOCK lines also occur in runs that completed, so neither explains it.
- User on runs 160/161: phone fine until the test started opening apps. Prime suspect (unproven; ring stops at the freeze):
  CoreImage's real CPU renderer, enabled for the first time by the run 160 key fix.
- Run 162 (boot r161, payload pl127 6e7e8a6a seq 6aef7beb, same 6-app mask, test starts after 120 s): J1_CI_SOFTWARE
  default 0 (key back to the ignored name); +[CUIShapeEffectStack sharedCIContext] -> nil in apps and SpringBoard (log CUIFIX).
- Run 162 result: no freeze; test completed. Alive at 15 s: Settings, Notes, Reminders (CUIFIX fired). Stocks crashes
  (QuartzCore +0xdcd8, NULL +0xc, recursive walk). Game Center aborts (x-alloc.c:567 slab_dealloc). Music killed 0x9 by
  backboardd (pid 29), likely the launch watchdog. Consistent with the CPU CoreImage renderer causing the 160/161 freezes
  (not directly observed: the ring stops at a freeze). UDA LIVELOCK lines clear after retries; no link to the QuartzCore
  failures established.
- App status over runs 157-162, launch test only (alive at 15 s, UI not verified): Settings, Notes, Photos, Camera, Contacts,
  Calendar, Reminders, Clock, Calculator, Weather, Maps, Safari, Mail, Messages, FaceTime, Videos, Voice Memos, Passbook,
  App Store, iTunes Store. Failing: Stocks, Game Center, Music. Nike rc=8 (not launchable).
- Run 163 (boot r161, payload pl128 c0a00665 seq 6aef7bec): normal build, no J1_APPTEST.
- Stocks crash decoded: QuartzCore +0xdcd8 `ldr r1,[r0,#0xc]` after `ldr r0,[r5]`: virtual call on an object whose vtable
  word is 0. With the slab_dealloc magic failure = memory not holding what was written (hypothesis).
- Run 164 (boot r164 f4f0becb, payload pl129 9378b1b7 seq 6aef7bed, test mask 0x117E27: Settings, Notes, Photos, Calendar,
  Weather, Stocks, Maps, Safari, Mail, Messages, Music, Game Center): pmap.c D-cache/walker holes: pmap_protect changed PTEs
  with no pte_sync; cache-attribute update likewise; pte_sync had no dsb; new L1/L2 tables (zeroed/copied through the
  cached direct map) were not cleaned (pte_sync_range). Watch: UDA LIVELOCK count should drop (162: many).
- Run 164 result: Game Center alive at 15 s (1 sample; failed in runs 157, 162), Stocks crashes identically (3 of 3 runs, so
  not random corruption), Music still killed by backboardd, Settings crashed once (CoreFoundation +0x46d4, bad object 0x5, lr
  0xa03b7 in a low-address image, not identified). UDA LIVELOCK went UP, 17 -> 223, 169 of them one page (04845000, memset);
  fsr 0x807 = translation fault, write. The run 164 pmap fixes are correct per pte_sync's own comment but did not explain
  these failures; whether r164 is better or worse than r161 is NOT established. pmap_enter same-PA path does UNLOCK_PVH(pa)
  after LOCK_PVH(pai): looks wrong, not changed, not checked.
- Run 165 (boot r164, payload pl130 9c7709cf seq 6aef7bee): normal build, no J1_APPTEST. r161 kept at s5/build/xnu-boot.r161.img.
- Run 166 (boot r161 94e65cd0, payload pl130): user chose to go back to r161; the run 164 pmap.c changes were removed from the
  source (scratchpad ptesync_undo.py).
