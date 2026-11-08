/* GL call tracer: logs the first call of every interposed gl* function to stderr (ends up in the serial log via J1WRITE). */
typedef unsigned int u32;

extern const char *const j1_names[];
extern const int j1_nnames;

static unsigned char seen[1024];
static u32 total;

static inline void sys_write(int fd, const char *p, int n)
{
    register int r0 __asm__("r0") = fd;
    register const char *r1 __asm__("r1") = p;
    register int r2 __asm__("r2") = n;
    register int r12 __asm__("r12") = 4;
    __asm__ volatile("svc #0x80" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r12) : "memory", "cc");
}

void j1_trace(int idx)
{
    char buf[64];
    int n = 0, i = 0;
    total++;
    if (idx < 0 || idx >= j1_nnames || seen[idx])
        return;
    seen[idx] = 1;
    buf[n++] = 'G';
    buf[n++] = 'L';
    buf[n++] = ' ';
    for (const char *s = j1_names[idx]; *s && n < 62; s++)
        buf[n++] = *s;
    buf[n++] = '\n';
    (void)i;
    sys_write(2, buf, n);
}

static int put_hex(char *b, int n, u32 v)
{
    for (int sh = 28; sh >= 0; sh -= 4)
        b[n++] = "0123456789abcdef"[(v >> sh) & 15];
    return n;
}

void j1_trace_slot(int off, u32 lr, const u32 *args)
{
    static unsigned char slot_seen[1024];
    char buf[96];
    int n = 0;
    const char *tag = "GLSLOT ";
    if (off < 0 || off >= 0x1000 || slot_seen[off >> 2])
        return;
    slot_seen[off >> 2] = 1;
    while (*tag)
        buf[n++] = *tag++;
    n = put_hex(buf, n, (u32)off);
    buf[n++] = ' ';
    n = put_hex(buf, n, lr);
    for (int i = 0; i < 4; i++) {
        buf[n++] = ' ';
        n = put_hex(buf, n, args[i]);
    }
    buf[n++] = '\n';
    sys_write(2, buf, n);
}

void j1_logbuf(const char *tag, const char *s, int n)
{
    static int count;
    char buf[200];
    int k = 0;
    if (count++ > 600)
        return;
    while (*tag && k < 10)
        buf[k++] = *tag++;
    buf[k++] = ' ';
    for (int i = 0; i < n && k < 190; i++)
        buf[k++] = (s[i] >= 32 && s[i] < 127) ? s[i] : '~';
    buf[k++] = '\n';
    sys_write(2, buf, k);
}

void j1_logstr(const char *tag, const char *s)
{
    static int count;
    char buf[100];
    int n = 0;
    if (count++ > 400)
        return;
    while (*tag && n < 10)
        buf[n++] = *tag++;
    buf[n++] = ' ';
    while (*s && n < 96)
        buf[n++] = *s++;
    buf[n++] = '\n';
    sys_write(2, buf, n);
}

__attribute__((constructor)) static void j1_loaded(void)
{
    static const char m[] = "J1SHIM trace loaded\n";
    sys_write(2, m, sizeof m - 1);
}
