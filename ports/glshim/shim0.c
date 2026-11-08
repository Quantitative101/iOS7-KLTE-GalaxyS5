/* Stage 0: prove DYLD_INSERT_LIBRARIES code runs inside backboardd. Dependency-free: raw syscalls only. */
typedef unsigned int u32;
static u32 fb[480 * 800];

static inline int sys(int n, int a)
{
    register int r0 __asm__("r0") = a;
    register int r12 __asm__("r12") = n;
    __asm__ volatile("svc #0x80" : "+r"(r0) : "r"(r12) : "memory", "cc");
    return r0;
}

__attribute__((constructor)) static void shim_init(void)
{
    for (int i = 0; i < 480 * 800; i++)
        fb[i] = (i / 480) < 400 ? 0xffff0000 : 0xff00ff00; /* top red, bottom green: proves injection */
    sys(500, (int)fb);
    {
        static const char msg[] = "J1SHIM injected\n";
        register int r0 __asm__("r0") = 2;
        register int r1 __asm__("r1") = (int)msg;
        register int r2 __asm__("r2") = sizeof msg - 1;
        register int r12 __asm__("r12") = 4;
        __asm__ volatile("svc #0x80" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r12) : "memory", "cc");
    }
}
