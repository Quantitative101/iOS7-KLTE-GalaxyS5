/* j1shim.dylib: thin loader injected into backboardd. It dlopens the swappable GL code (/var/tmp/j1gl.dylib) and restarts
 * the process (exit; launchd KeepAlive respawns it) whenever the reload daemon bumps the sequence number in /var/tmp/j1reload. */
typedef unsigned int u32;

extern void *dlopen(const char *path, int mode);
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, unsigned long n);
extern int close(int fd);
extern int usleep(unsigned int us);
extern int pthread_create(void *thr, const void *attr, void *(*fn)(void *), void *arg);
extern void exit(int code);

static u32 cur_seq(void)
{
    u32 v = 0;
    int fd = open("/var/tmp/j1reload", 0);
    if (fd >= 0) {
        if (read(fd, &v, 4) != 4)
            v = 0;
        close(fd);
    }
    return v;
}

static void say(const char *s)
{
    register int r0 __asm__("r0") = 2;
    register const char *r1 __asm__("r1") = s;
    register int r2 __asm__("r2") = 0;
    register int r12 __asm__("r12") = 4;
    while (s[r2])
        r2++;
    __asm__ volatile("svc #0x80" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r12) : "memory", "cc");
}

static void *watcher(void *arg)
{
    u32 base = (u32)arg;
    for (;;) {
        usleep(1000000);
        if (cur_seq() != base) {
            say("J1RELOAD new payload, restarting\n");
            exit(0);
        }
    }
    return 0;
}

__attribute__((constructor)) static void loader_init(void)
{
    u32 seq = cur_seq();
    char thr[64];
    void *h = dlopen("/var/tmp/j1gl.dylib", 2 /* RTLD_NOW */);
    say(h ? "J1LOADER gl loaded\n" : "J1LOADER gl missing\n");
    pthread_create(thr, 0, watcher, (void *)seq);
}
