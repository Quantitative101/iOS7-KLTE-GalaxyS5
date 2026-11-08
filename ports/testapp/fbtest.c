/* Freestanding armv7 iOS test app: paints a pattern and blits it with the bring-up syscall 500. */
typedef unsigned int u32;
#define W 480
#define H 800
static u32 fb[W * H];

static inline int sys(int n, int a)
{
    register int r0 __asm__("r0") = a;
    register int r12 __asm__("r12") = n;
    __asm__ volatile("svc #0x80" : "+r"(r0) : "r"(r12) : "memory", "cc");
    return r0;
}

static void rect(int x, int y, int w, int h, u32 c)
{
    for (int j = y; j < y + h && j < H; j++)
        for (int i = x; i < x + w && i < W; i++)
            fb[j * W + i] = c;
}

/* 5x7 glyph rows for "iOS 7" and "J1" drawn big */
static const unsigned char gJ[7] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c};
static const unsigned char g1[7] = {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
static const unsigned char gi[7] = {0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e};
static const unsigned char gO[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
static const unsigned char gS[7] = {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
static const unsigned char g7[7] = {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
static const unsigned char gsp[7] = {0};

static void glyph(const unsigned char *g, int x, int y, int s, u32 c)
{
    for (int r = 0; r < 7; r++)
        for (int q = 0; q < 5; q++)
            if (g[r] & (0x10 >> q))
                rect(x + q * s, y + r * s, s, s, c);
}

static inline int sys3(int n, int a, int b, int c)
{
    register int r0 __asm__("r0") = a;
    register int r1 __asm__("r1") = b;
    register int r2 __asm__("r2") = c;
    /* r12 is loaded inside the asm (not as a register variable: the compiler may use r12 as scratch before the svc).
     * BSD syscalls report errors with the carry flag set and errno in r0: turn that into -1. */
    __asm__ volatile("mov r12, %3\n\tsvc #0x80\n\tmvncs r0, #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(n) : "r12", "memory", "cc");
    return r0;
}

/* Reload daemon part: sector 1 of /dev/rdisk0 holds {'J1GL', seq, len} (tools/gl-push.py), payload from sector 2. A new
 * sequence number makes us write /var/tmp/j1gl.dylib and then /var/tmp/j1reload (the loader inside backboardd restarts
 * itself when it sees that value change). Results are reported on stderr (kernel J1WRITE shows them). */
static unsigned char pay[(1 << 20)] __attribute__((aligned(512)));
static u32 last_seq;

static void say(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    sys3(4, 2, (int)s, n);
}

static void reload_poll(void)
{
    int fd = sys3(5, (int)"/dev/rdisk0s3", 0, 0); /* open O_RDONLY */
    int n;
    u32 seq, len, total;
    if (fd < 0) {
        static int warned;
        if (!warned++) {
            int t;
            say("J1RELOADD cannot open /dev/rdisk0s3\n");
            sys3(24, 0, 0, 0); /* getuid: shows up in the kernel J1FPD trace */
            t = sys3(5, (int)"/dev/rdisk0s1", 0, 0);
            say(t >= 0 ? "J1RELOADD rdisk0s1 opens\n" : "J1RELOADD rdisk0s1 fails too\n");
            t = sys3(5, (int)"/dev/disk0s3", 0, 0);
            say(t >= 0 ? "J1RELOADD disk0s3 opens\n" : "J1RELOADD disk0s3 fails too\n");
        }
        return;
    }
    n = sys3(3, fd, (int)pay, 512); /* header sector */
    if (n != 512 || pay[0] != 'J' || pay[1] != '1' || pay[2] != 'G' || pay[3] != 'L') {
        static int dbg;
        if (dbg++ < 2)
            say("J1RELOADD no payload header yet\n");
        sys3(6, fd, 0, 0);
        return;
    }
    seq = *(u32 *)(pay + 4);
    len = *(u32 *)(pay + 8);
    if (seq == last_seq || len == 0 || len > (1 << 20) - 1024) {
        sys3(6, fd, 0, 0);
        return;
    }
    sys3(6, fd, 0, 0);
    fd = sys3(5, (int)"/dev/rdisk0s3", 0, 0);
    total = 512 + ((len + 511) & ~511u);
    n = sys3(3, fd, (int)pay, (int)total);
    sys3(6, fd, 0, 0);
    if (n != (int)total) {
        say("J1RELOADD short read\n");
        return;
    }
    fd = sys3(5, (int)"/var/tmp/j1gl.dylib", 0x601 /* O_WRONLY|O_CREAT|O_TRUNC */, 0755);
    if (fd < 0) {
        say("J1RELOADD cannot create j1gl.dylib\n");
        return;
    }
    sys3(4, fd, (int)(pay + 512), (int)len);
    sys3(6, fd, 0, 0);
    fd = sys3(5, (int)"/var/tmp/j1reload", 0x601, 0644);
    if (fd >= 0) {
        sys3(4, fd, (int)&seq, 4);
        sys3(6, fd, 0, 0);
    }
    last_seq = seq;
    say("J1RELOADD payload installed\n");
}

void start(void)
{
    static const u32 bars[8] = {0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00, 0xffff00ff, 0xffff0000, 0xff0000ff, 0xff000000};
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++) {
            u32 v = (u32)(j * 255 / H);
            fb[j * W + i] = 0xff000000 | (v << 16) | ((255 - v) << 8) | (u32)(i * 255 / W);
        }
    for (int b = 0; b < 8; b++)
        rect(b * 60, 0, 60, 200, bars[b]);
    const unsigned char *t[] = {gi, gO, gS, gsp, g7};
    for (int k = 0; k < 5; k++)
        glyph(t[k], 20 + k * 90, 260, 14, 0xffffffff);
    const unsigned char *u[] = {gJ, g1};
    for (int k = 0; k < 2; k++)
        glyph(u[k], 120 + k * 120, 460, 20, 0xff000000);
    rect(0, 0, W, 4, 0xffffffff);
    rect(0, H - 4, W, 4, 0xffffffff);
    rect(0, 0, 4, H, 0xffffffff);
    rect(W - 4, 0, 4, H, 0xffffffff);
    sys3(23, 0, 0, 0); /* setuid(0): allowed for this daemon by a J1 kernel exception */
    for (int tick = 0;; tick++) {
        if (tick % 3 == 0)
            sys(500, (int)fb);
        reload_poll();
        /* select(0,0,0,0,&tv) sleeps without burning the guest CPU. iOS 7 armv7 syscalls take arguments 5-7 in r4-r6 (not
         * on the stack), so the timeval pointer goes in r4; r4 is callee-saved, so it is pushed/popped around the svc.
         * r4 is loaded first: %0 may live in r0-r3, which are overwritten next (if %0 is r4 itself, the mov is a no-op). */
        static volatile int tv[2] = {1, 0};
        __asm__ volatile("push {r4}\n\tmov r4, %0\n\tmov r0, #0\n\tmov r1, #0\n\tmov r2, #0\n\tmov r3, #0\n\tmov r12, #93\n\t"
                         "svc #0x80\n\tpop {r4}"
                         :
                         : "r"(tv)
                         : "r0", "r1", "r2", "r3", "r12", "memory", "cc");
    }
}
