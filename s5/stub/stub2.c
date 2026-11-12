/* Galaxy S5 (SM-G900M klte, MSM8974PRO-AC) XNU stub, after tab2/stub/stub.c. Loaded by LK (aboot) as the boot image
 * "kernel" at 0x00008000. Copies the flattened XNU image to its physical base, builds XNU's boot_args and enters the
 * kernel like uniLoader does.
 *
 * Physical layout (RAM regions from /proc/iomem; LK's own use from the probe):
 *   0x00008000            this stub + payload (LK's load address)
 *   0x01e00000            LK's DTB, 0x02000000 LK's ramdisk (both unused)
 *   0x03200000            LK's continuous-splash scanout: RGB0 pipe, 1080x1920 packed RGB888, stride 3240. The kernel's
 *                         text console draws here (osfmk/arm/j1_board.h J1_S5) and pushes frames with MDP CTL0 START.
 *   0x1e000000            offscreen "framebuffer" for the kernel's video checks (480x800x32, as on the S4 Mini)
 *   0x1f000000            crumb page (stage words at +0xf00); log ring (header J1LG + 256 KiB text) at 0x1f010000
 *   0x20000000-0x40000000 XNU RAM: physBase 0x20000000, memSize 512 MiB (the iPad 2's size), inside System RAM
 *                         0x10200000-0x759fffff.
 */
typedef unsigned int u32;
typedef unsigned short u16;

#define PHYS_BASE 0x20000000u
#define VIRT_BASE 0x80000000u
#define MEMSIZE 0x20000000u
#define LOAD_PA (PHYS_BASE + 0x1000u)
#define RING_PA 0x1f010000u
#define CRUMBS 0x1f000f00u
#define OFFSCREEN_FB 0x1e000000u
#define CMDLINE "-v keepsyms=1 kprintf=1 debug=0x14e fips_mode=0 rd=disk0s1" /* rd: run 9 mounted disk0s3 (payload) as root */

/* LK splash scanout and the MDP CTL0 START register that pushes a frame to the command-mode panel */
#define FB 0x03200000u
#define FB_W 1080u
#define FB_H 1920u
#define FB_STRIDE 3240u /* 1080 * 3 */
#define MDP_CTL0_START 0xfd90061cu
/* Restart reason in shared IMEM: 0x77665502 = boot recovery. Set first thing, so any reset (button, hang) lands in TWRP. */
#define RESTART_REASON 0xfe80565cu
#define WDT 0xf9017000u

extern const unsigned char xnu_blob_start[], xnu_blob_end[], dtb_start[], dtb_end[];
extern void jump_to_xnu(u32 entry_pa, u32 boot_args_pa);

struct xnu_boot_video {
    u32 v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

struct xnu_boot_args {
    u16 Revision, Version;
    u32 virtBase, physBase, memSize, topOfKernelData;
    struct xnu_boot_video Video;
    u32 machineType;
    void *deviceTreeP; /* virtual address */
    u32 deviceTreeLength;
    char CommandLine[2048];
    u32 RamdiskPA, RamdiskSize;
};

static inline u32 R(u32 a) { return *(volatile u32 *)a; }
static inline void W(u32 a, u32 v) { *(volatile u32 *)a = v; }

static u32 align_up(u32 v, u32 a) { return (v + a - 1) & ~(a - 1); }

static void copy(void *dst, const void *src, u32 n)
{
    u32 *d = dst;
    const u32 *s = src;
    for (u32 i = 0; i < n / 4; i++)
        d[i] = s[i];
    for (u32 i = n & ~3u; i < n; i++)
        ((unsigned char *)dst)[i] = ((const unsigned char *)src)[i];
}

static void zero(void *dst, u32 n)
{
    unsigned char *d = dst;
    for (u32 i = 0; i < n; i++)
        d[i] = 0;
}

/* LC_UNIXTHREAD pc of the Mach-O header kept at the start of the flattened image */
static u32 find_entry_va(const unsigned char *img)
{
    u32 ncmds = ((const u32 *)img)[4];
    const unsigned char *p = img + 28;
    if (((const u32 *)img)[0] != 0xfeedfaceu || ncmds > 64)
        return 0;
    for (u32 i = 0; i < ncmds; i++) {
        u32 cmd = ((const u32 *)p)[0], size = ((const u32 *)p)[1];
        if (size < 8 || size > 0x10000u || (size & 3))
            return 0;
        if (cmd == 5 /* LC_UNIXTHREAD */)
            return ((const u32 *)p)[2 + 2 + 15]; /* flavor, count, then r0..r12, sp, lr, pc */
        p += size;
    }
    return 0;
}

/*
 * stub2.c: stub.c that loads the kernel where its own __TEXT segment says, instead of the fixed 0x80001000. Xcode 14.2's
 * linker put __TEXT at 0x80004000 (GitHub issue #1, entry 0x800ea8e0): the fixed load left every byte 0x3000 off and
 * the jump landed in the middle of the kernel. Falls back to LOAD_PA when the header has no usable __TEXT.
 * Use it by copying it over stub.c before s5/stub/build.sh.
 */
static u32 find_seg_va(const unsigned char *img, const char *name)
{
    u32 ncmds = ((const u32 *)img)[4];
    const unsigned char *p = img + 28;
    if (((const u32 *)img)[0] != 0xfeedfaceu || ncmds > 64)
        return 0;
    for (u32 i = 0; i < ncmds; i++) {
        u32 cmd = ((const u32 *)p)[0], size = ((const u32 *)p)[1];
        if (size < 8 || size > 0x10000u || (size & 3))
            return 0;
        if (cmd == 1 /* LC_SEGMENT */) {
            u32 k = 0;
            while (k < 16 && name[k] && p[8 + k] == (unsigned char)name[k])
                k++;
            if (k < 16 && !name[k] && !p[8 + k])
                return ((const u32 *)p)[6];
        }
        p += size;
    }
    return 0;
}

static u32 find_text_va(const unsigned char *img)
{
    u32 ncmds = ((const u32 *)img)[4];
    const unsigned char *p = img + 28;
    if (((const u32 *)img)[0] != 0xfeedfaceu || ncmds > 64)
        return 0;
    for (u32 i = 0; i < ncmds; i++) {
        u32 cmd = ((const u32 *)p)[0], size = ((const u32 *)p)[1];
        if (size < 8 || size > 0x10000u || (size & 3))
            return 0;
        if (cmd == 1 /* LC_SEGMENT */ && p[8] == '_' && p[9] == '_' && p[10] == 'T' && p[11] == 'E' && p[12] == 'X' &&
            p[13] == 'T' && p[14] == 0)
            return ((const u32 *)p)[6]; /* cmd, cmdsize, segname[16], then vmaddr */
        p += size;
    }
    return 0;
}

static void crumb(u32 idx, u32 v) { ((volatile u32 *)CRUMBS)[idx] = v; }

static void kick(void)
{
    __asm__ volatile("dsb" ::: "memory");
    W(MDP_CTL0_START, 1);
}

static void block(u32 x, u32 y, u32 s)
{
    for (u32 j = 0; j < s; j++) {
        volatile unsigned char *p = (volatile unsigned char *)(FB + (y + j) * FB_STRIDE + x * 3);
        for (u32 i = 0; i < s * 3; i++)
            p[i] = 0xff;
    }
}

/*
 * The last boot's crumbs on screen, if the reset kept RAM and the crumb page still starts with a stub mark (0x4a3155xx):
 * its 64 words as rows of 32 marks (24 px block = 1, 6 px dot = 0, most significant bit first), then ~10 s for a photo.
 */
static void show_previous_crumbs(void)
{
    volatile u32 *c = (volatile u32 *)CRUMBS;
    u32 c0 = c[0];
    if ((c0 & 0xffffff00u) != 0x4a315500u)
        return;
    c[0] = 0; /* shown once: a reset during the display must not show it again on every boot */
    for (u32 v = 0; v < 64; v++)
        for (u32 b = 0; b < 32; b++) {
            u32 big = ((v ? c[v] : c0) >> (31 - b)) & 1, sz = big ? 24 : 6;
            block(40 + b * 30 + (b / 4) * 4 + (24 - sz) / 2, 60 + v * 28 + (24 - sz) / 2, sz);
        }
    kick();
    for (volatile u32 d = 0; d < 0x04000000u; d++)
        ;
}

void stub_main(u32 dtb)
{
    u32 ksize = (u32)(xnu_blob_end - xnu_blob_start);
    u32 dtsize = (u32)(dtb_end - dtb_start);
    u32 dt_pa, ba_pa, entry_va;
    struct xnu_boot_args *ba;
    const char *cl = CMDLINE;

    W(RESTART_REASON, 0x77665502u);
    W(WDT + 0x08, 0); /* LK may have left it running: off while the old crumbs are on screen */
    show_previous_crumbs();
    /* APPS watchdog (msm_watchdog_v2 layout at 0xf9017000: RST +0x04, EN +0x08, BARK_TIME +0x10, BITE_TIME +0x14,
     * 32765 Hz sleep clock): bite after 45 s unless the kernel's tick pets it. With the restart reason above, a hang,
     * a panic or the end of the kernel's run window brings the phone back to TWRP without anyone pressing buttons. */
    W(WDT + 0x08, 0);
    W(WDT + 0x04, 1);
    W(WDT + 0x10, 45u * 32765u);
    W(WDT + 0x14, 45u * 32765u);
    W(WDT + 0x08, 1);
    ((volatile u32 *)RING_PA)[0] = 0;
    ((volatile u32 *)RING_PA)[1] = 0;
    for (u32 i = 0; i < 64; i++)
        crumb(i, 0);
    crumb(0, 0x4a315501); /* stub entered */
    crumb(7, dtb);
    crumb(1, ksize);
    crumb(8, R(0xfd901e24u)); /* RGB0 SRC_YSTRIDE0 as LK left it */
    block(40, 40, 48);    /* white square: the stub runs (the kernel console wipes the screen when it starts) */
    kick();
    u32 text_va = find_text_va((const unsigned char *)xnu_blob_start);
    u32 load_pa = LOAD_PA;
    if (text_va >= VIRT_BASE + 0x1000u && text_va < VIRT_BASE + 0x100000u && !(text_va & 0xfffu))
        load_pa = text_va - VIRT_BASE + PHYS_BASE;
    crumb(14, load_pa);   /* where the kernel went (0x20001000 for the usual build) */
    copy((void *)load_pa, xnu_blob_start, ksize);
    crumb(0, 0x4a315502); /* kernel copied */
    crumb(12, ((volatile u32 *)load_pa)[0]);
    crumb(13, ((volatile u32 *)load_pa)[4]);
    /* GitHub issue #1: the kernel read 0 from its first __DATA word (__nl_symbol_ptr). Row 15 = __DATA vmaddr, row 9 =
     * that word right after the copy, row 10 = the same word just before the jump. (Rows 16-18 belong to locore.s.) */
    {
        u32 data_va = find_seg_va((const unsigned char *)load_pa, "__DATA");
        u32 text_va2 = find_seg_va((const unsigned char *)load_pa, "__TEXT");
        crumb(15, data_va);
        if (data_va > text_va2 && text_va2 && data_va - text_va2 < ksize)
            crumb(9, ((volatile u32 *)(data_va - VIRT_BASE + PHYS_BASE))[0]);
    }
    entry_va = find_entry_va((const unsigned char *)load_pa);
    crumb(2, entry_va);
    if (!entry_va) {
        crumb(0, 0x4a3155eeu); /* no entry point: stop here, the next boot shows the crumbs */
        for (;;)
            __asm__ volatile("wfi");
    }
    dt_pa = align_up(load_pa + ksize + 0x1000000u, 4);
    copy((void *)dt_pa, dtb_start, dtsize);
    ba_pa = align_up(dt_pa + dtsize, 4);
    ba = (struct xnu_boot_args *)ba_pa;
    zero(ba, sizeof(*ba));
    ba->Revision = 1;
    ba->Version = 2;
    ba->virtBase = VIRT_BASE;
    ba->physBase = PHYS_BASE;
    ba->memSize = MEMSIZE;
    ba->topOfKernelData = align_up(ba_pa + sizeof(*ba), 1u << 20);
    ba->Video.v_baseAddr = OFFSCREEN_FB;
    ba->Video.v_rowBytes = 480 * 4;
    ba->Video.v_width = 480;
    ba->Video.v_height = 800;
    ba->Video.v_depth = 32;
    ba->machineType = 0;
    ba->deviceTreeP = (void *)(dt_pa - PHYS_BASE + VIRT_BASE);
    ba->deviceTreeLength = dtsize;
    for (int i = 0; cl[i]; i++)
        ba->CommandLine[i] = cl[i];
    crumb(3, ba_pa);
    {
        u32 data_va = ((volatile u32 *)CRUMBS)[15];
        if (data_va >= VIRT_BASE && data_va - VIRT_BASE < ksize + 0x1000u)
            crumb(10, ((volatile u32 *)(data_va - VIRT_BASE + PHYS_BASE))[0]);
    }
    crumb(0, 0x4a315504); /* jumping to XNU */
    jump_to_xnu(entry_va - VIRT_BASE + PHYS_BASE, ba_pa);
    for (;;)
        __asm__ volatile("wfi");
}
