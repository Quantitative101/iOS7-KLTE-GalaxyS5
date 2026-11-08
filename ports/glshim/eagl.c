/* Stage 1b: make EAGLContext "succeed" without a GPU so QuartzCore goes on to issue gl* calls, and log what it asks for. */
typedef unsigned int u32;
typedef void *id;
typedef void *SEL;
typedef void *Method;
typedef void *Class;
typedef id (*IMP)(id, SEL, ...);

extern Class objc_getClass(const char *);
extern Class object_getClass(id);
extern Method *class_copyMethodList(Class, unsigned int *);
extern SEL method_getName(Method);
extern IMP method_setImplementation(Method, IMP);
extern const char *sel_getName(SEL);

extern void j1_logstr(const char *tag, const char *s);

static id g_current;
static unsigned g_ctx[0x1100 / 4] __attribute__((aligned(16)));
extern void j1_fill_ctx(unsigned *t);

static void set_tls_ctx(void)
{
    unsigned base;
    __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(base));
    base &= ~3u;
    *(unsigned *)(base + 0x78) = (unsigned)g_ctx;
}

static void install_exc_handler(void);

/* run 104: EAGL calls happen every frame (swapNotification...): log only the first 200 */
static void eagl_log(SEL cmd)
{
    static int n;
    if (n < 200) {
        n++;
        j1_logstr("EAGL", sel_getName(cmd));
    }
}

static id get_macro(id self, SEL cmd, ...)
{
    install_exc_handler();
    eagl_log(cmd);
    /* macro context = TLS struct + 0x10: [0] = GL context, function table at the exported-stub offsets minus 0x10 */
    return (id)((char *)g_ctx + 0x10);
}

static id ret_self(id self, SEL cmd, ...)
{
    eagl_log(cmd);
    return self;
}

extern void j1_prof_tick(void);
static id swap_notification(id self, SEL cmd, ...)
{
    eagl_log(cmd);
    j1_prof_tick();
    return (id)1;
}

static id ret_one(id self, SEL cmd, ...)
{
    eagl_log(cmd);
    return (id)1;
}

static id set_current(id self, SEL cmd, id ctx)
{
    eagl_log(cmd);
    g_current = ctx;
    if (ctx)
        set_tls_ctx();
    return (id)1;
}

static id get_current(id self, SEL cmd, ...)
{
    eagl_log(cmd);
    return g_current;
}

extern void *dlsym(void *handle, const char *symbol);
extern void j1_attach_surface(u32 tex, u32 *px, int w, int h, int stride_px);
extern void j1_attach_bound(u32 *px, int w, int h, int stride_px);
extern void j1_logbuf(const char *tag, const char *s, int n);

/* backboardd aborts through an uncaught NSException; log its name and reason before the runtime terminates. */
extern SEL sel_registerName(const char *);
extern id objc_msgSend(id, SEL, ...);

static void log_cstr(const char *tag, const char *s)
{
    int n = 0;
    if (!s)
        return;
    while (s[n])
        n++;
    for (int o = 0; o < n && o < 900; o += 150)
        j1_logbuf(tag, s + o, n - o < 150 ? n - o : 150);
}

static void uncaught_exception(id exc)
{
    id (*get)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    const char *(*utf8)(id, SEL) = (const char *(*)(id, SEL))objc_msgSend;
    id name = exc ? get(exc, sel_registerName("name")) : 0;
    id reason = exc ? get(exc, sel_registerName("reason")) : 0;
    SEL u = sel_registerName("UTF8String");
    log_cstr("EXC-NAME", name ? utf8(name, u) : "(null)");
    log_cstr("EXC-REASON", reason ? utf8(reason, u) : "(null)");
}

static void install_exc_handler(void)
{
    static int done;
    void (*set)(void (*)(id));
    if (done)
        return;
    set = (void (*)(void (*)(id)))dlsym((void *)-2, "NSSetUncaughtExceptionHandler");
    if (set) {
        set(uncaught_exception);
        done = 1;
        j1_logstr("EXC", "handler installed");
    }
}

static void hex8(char *b, unsigned v)
{
    for (int i = 0; i < 8; i++)
        b[i] = "0123456789abcdef"[(v >> (28 - 4 * i)) & 15];
}

/* -[EAGLContext attachImage:toCoreSurface:invertedRender:]: make the GL image render straight into the surface memory.
 * Argument order is inferred from the selector (image name, surface, flag); the raw values are logged for the first calls
 * so a wrong guess shows up in the ring. */
extern void j1_log_frame(void);
static id attach_image(id self, SEL cmd, unsigned image, void *surface, unsigned inverted)
{
    static int logs;
    j1_log_frame();
    unsigned (*get_w)(void *) = (unsigned (*)(void *))dlsym((void *)-2, "IOSurfaceGetWidth");
    unsigned (*get_h)(void *) = (unsigned (*)(void *))dlsym((void *)-2, "IOSurfaceGetHeight");
    unsigned (*get_rb)(void *) = (unsigned (*)(void *))dlsym((void *)-2, "IOSurfaceGetBytesPerRow");
    void *(*get_base)(void *) = (void *(*)(void *))dlsym((void *)-2, "IOSurfaceGetBaseAddress");
    int (*lock)(void *, unsigned, void *) = (int (*)(void *, unsigned, void *))dlsym((void *)-2, "IOSurfaceLock");
    char line[64];
    unsigned w = 0, h = 0, rb = 0;
    void *base = 0;
    if (surface && (unsigned)surface > 0x1000 && get_w && get_h && get_rb && get_base) {
        if (lock)
            lock(surface, 0, 0);
        w = get_w(surface);
        h = get_h(surface);
        rb = get_rb(surface);
        base = get_base(surface);
    }
    if (logs++ < 6) {
        const char *tag = "ATTACH ";
        int n = 0;
        while (*tag)
            line[n++] = *tag++;
        hex8(line + n, image); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)surface); n += 8; line[n++] = ' ';
        hex8(line + n, inverted); n += 8; line[n++] = ' ';
        hex8(line + n, w); n += 8; line[n++] = ' ';
        hex8(line + n, h); n += 8; line[n++] = ' ';
        hex8(line + n, rb); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)base); n += 8;
        j1_logbuf("EAGL", line, n);
    }
    if (base && w && h && rb) {
        /* first argument is the GL target (0x0DE1 seen on the S4), the surface backs the currently bound texture */
        if (image == 0x0DE1)
            j1_attach_bound((u32 *)base, (int)w, (int)h, (int)(rb / 4));
        else
            j1_attach_surface(image, (u32 *)base, (int)w, (int)h, (int)(rb / 4));
    }
    return (id)1;
}

/* -[EAGLContext texImageIOSurface:target:internalFormat:width:height:format:type:plane:invert:]: CA's layer contents
 * (images, backing stores). It used to fall through to ret_one and bind nothing, so every image-backed layer sampled an
 * empty texture. 32-bit BGRA surfaces are sampled in place (later updates show up); other formats are converted using
 * the surface's own row pitch. */
extern void j1_tex_external(u32 id, u32 *px, int w, int h, int stride_px);
extern void j1_tex_from_mem(u32 id, const unsigned char *src, int w, int h, unsigned pitch, u32 fmt);
extern u32 j1_bound_texture(void);
static id tex_iosurface(id self, SEL cmd, void *surface, unsigned target, unsigned ifmt, unsigned w, unsigned h,
                        unsigned fmt, unsigned type, unsigned plane, unsigned invert)
{
    static int logs;
    unsigned (*get_rb)(void *) = (unsigned (*)(void *))dlsym((void *)-2, "IOSurfaceGetBytesPerRow");
    void *(*get_base)(void *) = (void *(*)(void *))dlsym((void *)-2, "IOSurfaceGetBaseAddress");
    unsigned (*get_rbp)(void *, unsigned) = (unsigned (*)(void *, unsigned))dlsym((void *)-2, "IOSurfaceGetBytesPerRowOfPlane");
    void *(*get_basep)(void *, unsigned) = (void *(*)(void *, unsigned))dlsym((void *)-2, "IOSurfaceGetBaseAddressOfPlane");
    unsigned rb = 0, tex = j1_bound_texture();
    void *base = 0;
    char line[96];
    if (surface && (unsigned)surface > 0x1000) {
        if (plane && get_basep && get_rbp) {
            base = get_basep(surface, plane);
            rb = get_rbp(surface, plane);
        } else if (get_base && get_rb) {
            base = get_base(surface);
            rb = get_rb(surface);
        }
    }
    if (logs++ < 12) {
        const char *tag = "TEXIOS ";
        int n = 0;
        unsigned v[9] = {tex, (unsigned)surface, w, h, fmt, type, plane, invert & 0xff, rb};
        while (*tag)
            line[n++] = *tag++;
        for (int i = 0; i < 9; i++) {
            hex8(line + n, v[i]);
            n += 8;
            line[n++] = ' ';
        }
        j1_logbuf("EAGL", line, n);
    }
    if (base && rb && w && h && tex) {
        if ((fmt == 0x80E1 || ifmt == 0x80E1) && (type == 0x1401 || type == 0x8367) && !(rb & 3))
            j1_tex_external(tex, (u32 *)base, (int)w, (int)h, (int)(rb / 4));
        else if (type == 0x1401)
            j1_tex_from_mem(tex, (const unsigned char *)base, (int)w, (int)h, rb, fmt);
    }
    return (id)1;
}

static int starts(const char *s, const char *p)
{
    while (*p)
        if (*s++ != *p++)
            return 0;
    return 1;
}

static void hook_class(Class c)
{
    unsigned int n = 0;
    Method *list = class_copyMethodList(c, &n);
    for (unsigned int i = 0; i < n; i++) {
        const char *name = sel_getName(method_getName(list[i]));
        IMP imp = ret_one;
        if (starts(name, "init"))
            imp = ret_self;
        else if (starts(name, "attachImage:toCoreSurface"))
            imp = (IMP)attach_image;
        else if (starts(name, "texImageIOSurface:target:internalFormat:width:height:format:type:plane:invert"))
            imp = (IMP)tex_iosurface;
        else if (starts(name, "swapNotification:"))
            imp = swap_notification;
        else if (starts(name, "getMacroContext"))
            imp = get_macro;
        else if (starts(name, "setCurrentContext"))
            imp = (IMP)set_current;
        else if (starts(name, "currentContext"))
            imp = get_current;
        else if (starts(name, "dealloc") || starts(name, "release") || starts(name, "retain") || starts(name, "alloc"))
            continue; /* leave memory management alone */
        method_setImplementation(list[i], imp);
    }
}

/* CoreAnimation's display lookup (IOMobileFramebufferGetMainDisplay) is IOServiceGetMatchingServices on
 * IOServiceMatching("IOMobileFramebuffer"); when that finds nothing backboardd throws "No window server display found"
 * (S5 runs 75, 77: every launch). This runs in backboardd before CA starts: count the matches, log them, and if there
 * are none wait (up to 15 s) for the service to appear, so a slow registration cannot kill backboardd. */
static void wait_for_framebuffer(void)
{
    void *(*matching)(const char *) = (void *(*)(const char *))dlsym((void *)-2, "IOServiceMatching");
    int (*get)(unsigned, void *, unsigned *) = (int (*)(unsigned, void *, unsigned *))dlsym((void *)-2, "IOServiceGetMatchingServices");
    unsigned (*next)(unsigned) = (unsigned (*)(unsigned))dlsym((void *)-2, "IOIteratorNext");
    int (*release)(unsigned) = (int (*)(unsigned))dlsym((void *)-2, "IOObjectRelease");
    int (*usleep_fn)(unsigned) = (int (*)(unsigned))dlsym((void *)-2, "usleep");
    char line[48];
    const char *tag = "FBWAIT ";
    unsigned found = 0, tries;
    int kr = 0, n = 0;
    if (!matching || !get || !next || !release || !usleep_fn)
        return;
    for (tries = 0; tries < 150; tries++) {
        unsigned it = 0, s;
        void *dict = matching("IOMobileFramebuffer");
        if (!dict)
            return;
        kr = get(0, dict, &it); /* consumes dict */
        found = 0;
        if (kr == 0 && it) {
            while ((s = next(it)) != 0) {
                found++;
                release(s);
            }
            release(it);
        }
        if (found)
            break;
        usleep_fn(100000);
    }
    while (*tag)
        line[n++] = *tag++;
    hex8(line + n, found); n += 8; line[n++] = ' ';
    hex8(line + n, tries); n += 8; line[n++] = ' ';
    hex8(line + n, (unsigned)kr); n += 8;
    j1_logbuf("EAGL", line, n);

    /* Run 78: the service matches at once and backboardd still fails, so try CA's own first two calls and log them:
     * IOMobileFramebufferGetMainDisplay(&fb) and IOMobileFramebufferGetDisplaySize(fb, &CGSize). */
    {
        int (*main_display)(void **) = (int (*)(void **))dlsym((void *)-2, "IOMobileFramebufferGetMainDisplay");
        int (*display_size)(void *, float *) = (int (*)(void *, float *))dlsym((void *)-2, "IOMobileFramebufferGetDisplaySize");
        void *fb = 0;
        float sz[2] = {0, 0};
        int r1 = -1, r2 = -1;
        if (main_display)
            r1 = main_display(&fb);
        if (r1 == 0 && fb && display_size)
            r2 = display_size(fb, sz);
        n = 0;
        tag = "FBMAIN ";
        while (*tag)
            line[n++] = *tag++;
        hex8(line + n, (unsigned)r1); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)fb); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)r2); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)sz[0]); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)sz[1]); n += 8;
        j1_logbuf("EAGL", line, n);
    }
}

/* Run 81: the open above always succeeds, yet backboardd still dies. The assert is in BKDisplayStartWindowServer()
 * (BKDisplay.m:336): display = [[CAWindowServer server] displayWithDisplayId:[[CADisplay mainDisplay] displayId]];
 * nil -> "No window server display found". The window server has not created its display object yet when backboardd
 * asks; whether it has is a race on our one slow core. Retry a nil answer for up to 15 s, sleeping so CA's threads run. */
static id (*orig_display_with_id)(id, SEL, unsigned);
static id display_with_id_retry(id self, SEL cmd, unsigned display_id)
{
    int (*usleep_fn)(unsigned) = (int (*)(unsigned))dlsym((void *)-2, "usleep");
    id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    unsigned (*send_u)(id, SEL) = (unsigned (*)(id, SEL))objc_msgSend;
    id d = display_id ? orig_display_with_id(self, cmd, display_id) : 0;
    unsigned waits = 0;
    while (!d && waits < 150 && usleep_fn) {
        usleep_fn(100000);
        waits++;
        /* Run 83: after a backboardd restart [CADisplay mainDisplay] was still nil when BKDisplay asked, so the id was 0
         * and could never match. Re-read the main display's id until it exists. */
        if (!display_id) {
            id cad = (id)objc_getClass("CADisplay");
            id main = cad ? send(cad, sel_registerName("mainDisplay")) : 0;
            display_id = main ? send_u(main, sel_registerName("displayId")) : 0;
            if (!display_id)
                continue;
        }
        d = orig_display_with_id(self, cmd, display_id);
    }
    {
        char line[48];
        const char *tag = "DISPWAIT ";
        int n = 0;
        while (*tag)
            line[n++] = *tag++;
        hex8(line + n, display_id); n += 8; line[n++] = ' ';
        hex8(line + n, waits); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)d); n += 8;
        j1_logbuf("EAGL", line, n);
    }
    return d;
}

/* Runs 84-96: every backboardd whose display lookup had to wait later aborted on the first touch with
 * "Assertion failed: (BKDisplayGetMainWindowServerDisplay())" in shiftDigitizerLocationToHitContext. In
 * BKDisplayStartWindowServer, [CADisplay mainDisplay] is read before displayWithDisplayId:; when CA is slow it is nil,
 * and although the lookup below then succeeds, backboardd's nil mainDisplay makes it skip storing the main window
 * server display (the only writer of that global, str at 0xadac) and the rest of the setup. Retry +[CADisplay
 * mainDisplay] itself (100 ms steps, 15 s) so backboardd sees the display on its own path. */
static id (*orig_main_display)(id, SEL);
static id main_display_retry(id self, SEL cmd)
{
    int (*usleep_fn)(unsigned) = (int (*)(unsigned))dlsym((void *)-2, "usleep");
    id d = orig_main_display(self, cmd);
    unsigned waits = 0;
    static int logged;
    while (!d && waits < 150 && usleep_fn) {
        usleep_fn(100000);
        waits++;
        d = orig_main_display(self, cmd);
    }
    if (waits && logged++ < 4) {
        char line[40];
        const char *tag = "MAINDISP ";
        int n = 0;
        while (*tag)
            line[n++] = *tag++;
        hex8(line + n, waits); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)d); n += 8;
        j1_logbuf("EAGL", line, n);
    }
    return d;
}

static void hook_main_display(void)
{
    Method (*get_cmethod)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getClassMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("CADisplay");
    Method m = (c && get_cmethod && get_imp) ? get_cmethod(c, sel_registerName("mainDisplay")) : 0;
    if (!m) {
        j1_logstr("EAGL", "MAINDISP no +[CADisplay mainDisplay]");
        return;
    }
    orig_main_display = (id (*)(id, SEL))get_imp(m);
    method_setImplementation(m, (IMP)main_display_retry);
}

static void hook_display_lookup(void)
{
    /* not in the linked libobjc stub: resolve at run time */
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class ws = objc_getClass("CAWindowServer");
    Method m = (ws && get_method && get_imp) ? get_method(ws, sel_registerName("displayWithDisplayId:")) : 0;
    if (!m) {
        j1_logstr("EAGL", "DISPWAIT no CAWindowServer displayWithDisplayId:");
        return;
    }
    orig_display_with_id = (id (*)(id, SEL, unsigned))get_imp(m);
    method_setImplementation(m, (IMP)display_with_id_retry);
}

/* SpringBoard (loaded through the kernel's DYLD_INSERT_LIBRARIES, S5 run 85): log every NSException as it is created,
 * with name, reason and a frame-pointer backtrace, because SpringBoard aborts on one and nothing records it. */
static id (*orig_exc_init)(id, SEL, id, id, id);
static id exc_init_log(id self, SEL cmd, id name, id reason, id info)
{
    static int count;
    if (count++ < 40) {
        const char *(*utf8)(id, SEL) = (const char *(*)(id, SEL))objc_msgSend;
        SEL u = sel_registerName("UTF8String");
        unsigned *fp = (unsigned *)__builtin_frame_address(0);
        char line[120];
        int n = 0;
        log_cstr("EXCNEW-NAME", name ? utf8(name, u) : "(nil)");
        log_cstr("EXCNEW-REASON", reason ? utf8(reason, u) : "(nil)");
        for (int i = 0; i < 12 && fp && ((unsigned)fp & 3) == 0 && n < 108; i++) {
            hex8(line + n, fp[1]);
            n += 8;
            line[n++] = ' ';
            fp = (unsigned *)fp[0];
        }
        j1_logbuf("EXCNEW-BT", line, n);
    }
    return orig_exc_init(self, cmd, name, reason, info);
}

/* Run 85: every SpringBoard abort was CALayerInvalidGeometry "position contains NaN" from
 * -[SBChevronView _layoutGrabberView:forState:] -> UIRectIntegralWithScale(rect, scale) with SpringBoard's cached
 * [[UIScreen mainScreen] scale]. When SpringBoard comes up before backboardd's display is ready the scale reads 0 and
 * the rounding divides by zero. The iPad 2 screen is scale 1: never let UIScreen answer 0 or NaN. */
static float (*orig_screen_scale)(id, SEL);
static float screen_scale_fixed(id self, SEL cmd)
{
    static int logs;
    float s = orig_screen_scale(self, cmd);
    if (!(s > 0.0f)) { /* also catches NaN */
        if (logs++ < 8)
            j1_logstr("SCALEFIX", "UIScreen scale was 0/NaN, answering 1.0");
        s = 1.0f;
    }
    return s;
}

static void fix_screen_scale(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("UIScreen");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("scale")) : 0;
    if (!m) {
        j1_logstr("SCALEFIX", "no -[UIScreen scale]");
        return;
    }
    orig_screen_scale = (float (*)(id, SEL))get_imp(m);
    method_setImplementation(m, (IMP)screen_scale_fixed);
}

/* Run 86: with the scale fixed, SpringBoard still aborted at startup: PhysicsKit (UIKit Dynamics) got nil bodies and
 * "-[__NSArrayM insertObject:atIndex:]: object cannot be nil" -- same race, a screen of size 0 because SpringBoard
 * started before backboardd's display existed. Hold SpringBoard (before main) until CA reports a main display. */
static void springboard_wait_for_display(void)
{
    int (*usleep_fn)(unsigned) = (int (*)(unsigned))dlsym((void *)-2, "usleep");
    id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    unsigned (*send_u)(id, SEL) = (unsigned (*)(id, SEL))objc_msgSend;
    id cad = (id)objc_getClass("CADisplay");
    unsigned display_id = 0, waits = 0;
    char line[40];
    const char *tag = "SBWAIT ";
    int n = 0;
    while (cad && usleep_fn && waits < 200) {
        id main = send(cad, sel_registerName("mainDisplay"));
        display_id = main ? send_u(main, sel_registerName("displayId")) : 0;
        if (display_id)
            break;
        usleep_fn(200000);
        waits++;
    }
    while (*tag)
        line[n++] = *tag++;
    hex8(line + n, waits); n += 8; line[n++] = ' ';
    hex8(line + n, display_id); n += 8;
    j1_logbuf("EAGL", line, n);
}

/* Run 87: the display existed before SpringBoard started (SBWAIT 0) yet the scale was still 0 (SCALEFIX x96) and the
 * PhysicsKit nil-body abort stayed: UIKit's screen geometry comes from MobileGestalt (main-screen-*), which is broken
 * here. Log MG's answers, and make UIScreen's bounds/applicationFrame non-empty (480x800, the GUI size). Struct
 * returns: the IMP gets the result pointer first. */
typedef struct { float x, y, w, h; } j1rect;
static void (*orig_screen_bounds)(j1rect *, id, SEL);
static void (*orig_screen_appframe)(j1rect *, id, SEL);
static void screen_bounds_fixed(j1rect *r, id self, SEL cmd)
{
    static int logs;
    orig_screen_bounds(r, self, cmd);
    if (!(r->w > 0.0f) || !(r->h > 0.0f)) {
        if (logs++ < 8)
            j1_logstr("SCALEFIX", "UIScreen bounds empty, answering 480x800");
        r->x = r->y = 0.0f;
        r->w = 480.0f;
        r->h = 800.0f;
    }
}
static void screen_appframe_fixed(j1rect *r, id self, SEL cmd)
{
    static int logs;
    orig_screen_appframe(r, self, cmd);
    if (!(r->w > 0.0f) || !(r->h > 0.0f)) {
        if (logs++ < 8)
            j1_logstr("SCALEFIX", "UIScreen applicationFrame empty, answering 0,20,480x780");
        r->x = 0.0f;
        r->y = 20.0f;
        r->w = 480.0f;
        r->h = 780.0f;
    }
}

static void fix_screen_geometry(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("UIScreen");
    Method m;
    if (!c || !get_method || !get_imp)
        return;
    if ((m = get_method(c, sel_registerName("bounds"))) != 0) {
        orig_screen_bounds = (void (*)(j1rect *, id, SEL))get_imp(m);
        method_setImplementation(m, (IMP)screen_bounds_fixed);
    }
    if ((m = get_method(c, sel_registerName("applicationFrame"))) != 0) {
        orig_screen_appframe = (void (*)(j1rect *, id, SEL))get_imp(m);
        method_setImplementation(m, (IMP)screen_appframe_fixed);
    }
}

/* MGCopyAnswer(CFStringRef) -> CFTypeRef; log the screen keys UIKit relies on (value as float, or the type id). */
static void log_mg_screen(void)
{
    void *(*mg)(void *, void *) = (void *(*)(void *, void *))dlsym((void *)-2, "MGCopyAnswer");
    void *(*cfstr)(void *, const char *, unsigned) = (void *(*)(void *, const char *, unsigned))dlsym((void *)-2, "CFStringCreateWithCString");
    int (*getval)(void *, int, void *) = (int (*)(void *, int, void *))dlsym((void *)-2, "CFNumberGetValue");
    unsigned long (*typeid_fn)(void *) = (unsigned long (*)(void *))dlsym((void *)-2, "CFGetTypeID");
    unsigned long (*numtype)(void) = (unsigned long (*)(void))dlsym((void *)-2, "CFNumberGetTypeID");
    static const char *keys[] = {"main-screen-scale", "main-screen-width", "main-screen-height", "DeviceClass"};
    if (!mg || !cfstr || !getval || !typeid_fn || !numtype)
        return;
    for (int i = 0; i < 4; i++) {
        void *k = cfstr(0, keys[i], 0x08000100 /* UTF8 */);
        void *v = k ? mg(k, 0) : 0;
        float f = -1.0f;
        char line[64];
        int n = 0;
        const char *t = keys[i];
        if (v && typeid_fn(v) == numtype())
            getval(v, 12 /* kCFNumberFloatType */, &f);
        while (*t && n < 30)
            line[n++] = *t++;
        line[n++] = ' ';
        hex8(line + n, (unsigned)v); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)(f * 1000.0f)); n += 8;
        j1_logbuf("MGANS", line, n);
    }
}

/* Run 91: with the scale fixed, the remaining SpringBoard abort is CALayer position NaN from
 * -[SBCenterIconZoomAnimator _prepareAnimation] during the unlock -> home screen "unscatter" animation (reached via
 * SBUIAnimationFadeAlertToSpringBoard -> SBUIController restoreContentAndUnscatterIconsAnimated: ->
 * SBIconController/SBFolderController unscatterAnimated:afterDelay:withCompletion:). Show the home screen without
 * that animation: always pass animated:NO. */
static void (*orig_unscatter)(id, SEL, signed char, double, id);
static void unscatter_no_anim(id self, SEL cmd, signed char animated, double delay, id completion)
{
    static int logs;
    if (animated && logs++ < 8)
        j1_logstr("SBFIX", "unscatter animated -> NO");
    orig_unscatter(self, cmd, 0, delay, completion);
}

/* Run 99: SpringBoard aborted (QuartzCore slab_dealloc magic assert, heap corruption) right after a touch landed on
 * an app icon, i.e. in the app-launch transition. Launch "scatters" the icons with the same icon animator that made
 * NaN positions on unscatter (run 91). Do it without animation too: -[SBIconController scatterAnimated:withCompletion:]
 * (v16@0:4c8@?12) always gets animated NO. */
static void (*orig_scatter)(id, SEL, signed char, id);
static void scatter_no_anim(id self, SEL cmd, signed char animated, id completion)
{
    static int logs;
    if (animated && logs++ < 8)
        j1_logstr("SBFIX", "scatter animated -> NO");
    orig_scatter(self, cmd, 0, completion);
}

static void springboard_no_scatter_animation(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("SBIconController");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("scatterAnimated:withCompletion:")) : 0;
    if (!m) {
        j1_logstr("SBFIX", "no -[SBIconController scatterAnimated:withCompletion:]");
        return;
    }
    orig_scatter = (void (*)(id, SEL, signed char, id))get_imp(m);
    method_setImplementation(m, (IMP)scatter_no_anim);
}

static void springboard_no_unscatter_animation(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("SBIconController");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("unscatterAnimated:afterDelay:withCompletion:")) : 0;
    if (!m) {
        j1_logstr("SBFIX", "no -[SBIconController unscatterAnimated:afterDelay:withCompletion:]");
        return;
    }
    orig_unscatter = (void (*)(id, SEL, signed char, double, id))get_imp(m);
    method_setImplementation(m, (IMP)unscatter_no_anim);
}

/* Run 93: with installd back, SpringBoard launches Setup.app (Setup Assistant) because com.apple.purplebuddy has no
 * SetupDone. Redirect SpringBoard's own CFPreferences imports (lazy pointers in its __DATA, unslid VAs from
 * otool -Iv: CopyAppValue 0x329cfc, GetAppBooleanValue 0x329d04) to wrappers that answer true for SetupDone /
 * SetupFinishedAllSteps in com.apple.purplebuddy, and log every purplebuddy key asked for. */
static void *(*real_copy_app_value)(void *, void *);
static unsigned char (*real_get_app_bool)(void *, void *, unsigned char *);
static int (*cf_getcstr)(void *, char *, int, unsigned);

static int cfstr_is(void *s, const char *want)
{
    char buf[64];
    if (!s || !cf_getcstr || !cf_getcstr(s, buf, sizeof buf, 0x08000100))
        return 0;
    for (int i = 0;; i++) {
        if (buf[i] != want[i])
            return 0;
        if (!buf[i])
            return 1;
    }
}

static int buddy_key(void *key, void *app)
{
    static int logs;
    char buf[64];
    if (!cfstr_is(app, "com.apple.purplebuddy"))
        return 0;
    if (logs++ < 16 && cf_getcstr && cf_getcstr(key, buf, sizeof buf, 0x08000100))
        j1_logstr("BUDDY", buf);
    return cfstr_is(key, "SetupDone") || cfstr_is(key, "SetupFinishedAllSteps");
}

static void *copy_app_value_fix(void *key, void *app)
{
    if (buddy_key(key, app)) {
        void **t = (void **)dlsym((void *)-2, "kCFBooleanTrue");
        void *(*retain)(void *) = (void *(*)(void *))dlsym((void *)-2, "CFRetain");
        return (t && retain) ? retain(*t) : 0;
    }
    return real_copy_app_value(key, app);
}

static unsigned char get_app_bool_fix(void *key, void *app, unsigned char *valid)
{
    if (buddy_key(key, app)) {
        if (valid)
            *valid = 1;
        return 1;
    }
    return real_get_app_bool(key, app, valid);
}

/* Run 94: SpringBoard only asked purplebuddy for SetupState (compared with RestoredFromiTunesBackup) and still ran
 * Setup.app: the device is unactivated. -[SBLockdownManager brickedDevice] is SpringBoard's "needs activation" flag;
 * answer NO and log what it and lockdownState really said. */
static signed char (*orig_bricked)(id, SEL);
static int (*orig_lockdown_state)(id, SEL);
static signed char bricked_no(id self, SEL cmd)
{
    static int logs;
    signed char b = orig_bricked(self, cmd);
    if (logs++ < 6) {
        char line[40];
        const char *tag = "bricked=";
        int n = 0;
        while (*tag)
            line[n++] = *tag++;
        hex8(line + n, (unsigned)(unsigned char)b); n += 8;
        line[n++] = ' ';
        hex8(line + n, orig_lockdown_state ? (unsigned)orig_lockdown_state(self, sel_registerName("lockdownState")) : 0xffffffffu);
        n += 8;
        j1_logbuf("BUDDY", line, n);
    }
    return 0;
}

/* Run 95: Setup.app still launched. -[SBApplicationController setupApplication] hands SpringBoard the Setup app it
 * launches; answer nil (as on a system without Setup.app) so there is nothing to launch. */
static id setup_app_nil(id self, SEL cmd)
{
    static int logs;
    if (logs++ < 4)
        j1_logstr("BUDDY", "setupApplication -> nil");
    return 0;
}

static void springboard_no_setup_app(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    Class c = objc_getClass("SBApplicationController");
    Method m = (c && get_method) ? get_method(c, sel_registerName("setupApplication")) : 0;
    if (!m) {
        j1_logstr("BUDDY", "no -[SBApplicationController setupApplication]");
        return;
    }
    method_setImplementation(m, (IMP)setup_app_nil);
}

/* Run 96: after the unlock touch events still reach backboardd's queue but the home screen ignores them. Log which
 * SpringBoard window receives each event (class name, window level, whether it is hidden / key) for the first ones. */
static const char *(*class_name_of)(id); /* object_getClassName, via dlsym (not in the linked libobjc stub) */
static void (*orig_window_send_event)(id, SEL, id);
static void window_send_event_log(id self, SEL cmd, id event)
{
    static int logs;
    if (logs < 30) {
        float (*get_level)(id, SEL) = (float (*)(id, SEL))objc_msgSend;
        signed char (*get_bool)(id, SEL) = (signed char (*)(id, SEL))objc_msgSend;
        char line[120];
        const char *cn = class_name_of ? class_name_of(self) : "?";
        int n = 0;
        unsigned flags;
        logs++;
        while (cn && *cn && n < 60)
            line[n++] = *cn++;
        line[n++] = ' ';
        flags = (unsigned)(get_bool(self, sel_registerName("isHidden")) & 1) |
                (unsigned)(get_bool(self, sel_registerName("isKeyWindow")) & 1) << 4 |
                (unsigned)(get_bool(self, sel_registerName("isUserInteractionEnabled")) & 1) << 8;
        hex8(line + n, flags); n += 8; line[n++] = ' ';
        hex8(line + n, (unsigned)(int)get_level(self, sel_registerName("windowLevel"))); n += 8;
        j1_logbuf("SBEVT", line, n);
    }
    /* Run 98: SBAppWindow gets the touches on the home screen but nothing reacts. Log the view each touch landed on
     * and every ancestor with userInteractionEnabled NO (once per distinct view, for the first 60 touches). */
    {
        static int vlogs;
        static id last_view;
        id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
        signed char (*get_bool)(id, SEL) = (signed char (*)(id, SEL))objc_msgSend;
        id touches = send(event, sel_registerName("allTouches"));
        id touch = touches ? send(touches, sel_registerName("anyObject")) : 0;
        id view = touch ? send(touch, sel_registerName("view")) : 0;
        if (vlogs < 60 && view != last_view) {
            char line[160];
            int n = 0;
            id v = view;
            vlogs++;
            last_view = view;
            for (int depth = 0; v && depth < 12 && n < 150; depth++) {
                const char *cn = class_name_of ? class_name_of(v) : "?";
                int uie = get_bool(v, sel_registerName("isUserInteractionEnabled")) & 1;
                if (depth == 0 || !uie) {
                    while (cn && *cn && n < 140)
                        line[n++] = *cn++;
                    line[n++] = uie ? '+' : '-';
                    line[n++] = ' ';
                }
                v = send(v, sel_registerName("superview"));
            }
            if (!view) {
                const char *nv = "(no view)";
                while (*nv)
                    line[n++] = *nv++;
            }
            j1_logbuf("SBVIEW", line, n);
        }
    }
    orig_window_send_event(self, cmd, event);
}

static void springboard_log_events(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("UIWindow");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("sendEvent:")) : 0;
    class_name_of = (const char *(*)(id))dlsym((void *)-2, "object_getClassName");
    if (!m)
        return;
    orig_window_send_event = (void (*)(id, SEL, id))get_imp(m);
    method_setImplementation(m, (IMP)window_send_event_log);
}

/* Template icons (run 96): FaceTime, Calendar, Reminders, Settings ship only "~ipad" icon files, and our 480x800
 * screen makes iOS look for iPhone-class names. In SpringBoard, when -[NSBundle pathForResource:ofType:(inDirectory:)]
 * misses for an image, retry with "~ipad" inserted before the extension; log icon-ish misses and fallbacks so the
 * next run shows whether icons are looked up this way at all. */
static id (*orig_path_of_type)(id, SEL, id, id);
static id (*orig_path_of_type_dir)(id, SEL, id, id, id);
static int name_is_imageish(id name, id type)
{
    const char *(*utf8)(id, SEL) = (const char *(*)(id, SEL))objc_msgSend;
    SEL u = sel_registerName("UTF8String");
    const char *n = name ? utf8(name, u) : 0, *t = type ? utf8(type, u) : 0;
    int len = 0;
    if (!n)
        return 0;
    while (n[len])
        len++;
    if (t && t[0] == 'p' && t[1] == 'n' && t[2] == 'g' && !t[3])
        return 1;
    return len > 4 && n[len - 4] == '.' && n[len - 3] == 'p' && n[len - 2] == 'n' && n[len - 1] == 'g';
}

/* name (+ optional ".png") -> name~ipad (+ ".png"); 0 when it already says ~ipad */
static id ipad_name(id name)
{
    id (*send1)(id, SEL, id) = (id (*)(id, SEL, id))objc_msgSend;
    id (*send0)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    id (*str)(id, SEL, const char *) = (id (*)(id, SEL, const char *))objc_msgSend;
    id nsstring = (id)objc_getClass("NSString");
    id suffix = str(nsstring, sel_registerName("stringWithUTF8String:"), "~ipad");
    id ext = send0(name, sel_registerName("pathExtension"));
    id base = send0(name, sel_registerName("stringByDeletingPathExtension"));
    unsigned (*len)(id, SEL) = (unsigned (*)(id, SEL))objc_msgSend;
    id r;
    if ((signed char)(long)send1(base, sel_registerName("hasSuffix:"), suffix))
        return 0;
    r = send1(base, sel_registerName("stringByAppendingString:"), suffix);
    if (ext && len(ext, sel_registerName("length")))
        r = send1(r, sel_registerName("stringByAppendingPathExtension:"), ext);
    return r;
}

static void log_lookup(const char *what, id name)
{
    static int logs;
    const char *(*utf8)(id, SEL) = (const char *(*)(id, SEL))objc_msgSend;
    if (logs++ < 40 && name)
        log_cstr(what, utf8(name, sel_registerName("UTF8String")));
}

static id path_of_type_fallback(id self, SEL cmd, id name, id type)
{
    id r = orig_path_of_type(self, cmd, name, type);
    if (!r && name && name_is_imageish(name, type)) {
        id alt = ipad_name(name);
        r = alt ? orig_path_of_type(self, cmd, alt, type) : 0;
        log_lookup(r ? "ICONFB-OK" : "ICONMISS", name);
    }
    return r;
}

static id path_of_type_dir_fallback(id self, SEL cmd, id name, id type, id dir)
{
    id r = orig_path_of_type_dir(self, cmd, name, type, dir);
    if (!r && name && name_is_imageish(name, type)) {
        id alt = ipad_name(name);
        r = alt ? orig_path_of_type_dir(self, cmd, alt, type, dir) : 0;
        log_lookup(r ? "ICONFB-OK" : "ICONMISS", name);
    }
    return r;
}

static void springboard_ipad_icon_fallback(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("NSBundle");
    Method m1 = (c && get_method && get_imp) ? get_method(c, sel_registerName("pathForResource:ofType:")) : 0;
    Method m2 = (c && get_method && get_imp) ? get_method(c, sel_registerName("pathForResource:ofType:inDirectory:")) : 0;
    if (m1) {
        orig_path_of_type = (id (*)(id, SEL, id, id))get_imp(m1);
        method_setImplementation(m1, (IMP)path_of_type_fallback);
    }
    if (m2) {
        orig_path_of_type_dir = (id (*)(id, SEL, id, id, id))get_imp(m2);
        method_setImplementation(m2, (IMP)path_of_type_dir_fallback);
    }
}

/* Settings labels (run 101): apps ship only "~ipad" string tables (Settings~ipad.strings, Localizable~ipad.strings)
 * and our 480x800 screen makes CFBundle look for the plain names, so lookups miss and the raw key shows
 * ("DO_NOT_DISTURB", "CASTLE"). When -[NSBundle localizedStringForKey:value:table:] misses, retry with the table
 * name + "~ipad". */
static id (*orig_localized)(id, SEL, id, id, id);
static id localized_fallback(id self, SEL cmd, id key, id value, id table)
{
    id r = orig_localized(self, cmd, key, value, table);
    id (*send1)(id, SEL, id) = (id (*)(id, SEL, id))objc_msgSend;
    id (*str)(id, SEL, const char *) = (id (*)(id, SEL, const char *))objc_msgSend;
    static int logs;
    id tbl, alt, r2;
    if (!key || !r)
        return r;
    if (r != key && (!value || r != value) &&
        !(signed char)(long)send1(r, sel_registerName("isEqualToString:"), key))
        return r; /* found */
    tbl = table ? table : str((id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "Localizable");
    alt = ipad_name(tbl);
    if (!alt)
        return r;
    r2 = orig_localized(self, cmd, key, value, alt);
    if (r2 && r2 != key && (!value || r2 != value) &&
        !(signed char)(long)send1(r2, sel_registerName("isEqualToString:"), key)) {
        if (logs++ < 8)
            log_lookup("STRFB-OK", key);
        return r2;
    }
    {
        /* run 105: which keys still miss, and from which table (Settings sub-menus) */
        static int misses;
        const char *(*utf8)(id, SEL) = (const char *(*)(id, SEL))objc_msgSend;
        if (misses++ < 30) {
            log_cstr("STRMISS", utf8(key, sel_registerName("UTF8String")));
            log_cstr("STRMISS-T", utf8(tbl, sel_registerName("UTF8String")));
        }
    }
    return r;
}

static void app_ipad_strings_fallback(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("NSBundle");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("localizedStringForKey:value:table:")) : 0;
    if (!m) {
        j1_logstr("STRFB", "no -[NSBundle localizedStringForKey:value:table:]");
        return;
    }
    orig_localized = (id (*)(id, SEL, id, id, id))get_imp(m);
    method_setImplementation(m, (IMP)localized_fallback);
}

/* Tiny centred app window (runs 100-101): SpringBoard runs every app in "classic" mode (the 320x480 letterbox for
 * apps without a launch image for this screen size). Report every app as non-classic. */
static signed char (*orig_is_classic)(id, SEL);
static signed char is_classic_no(id self, SEL cmd)
{
    static int logs;
    if (logs < 6 && orig_is_classic) {
        logs++;
        log_cstr("CLASSIC", orig_is_classic(self, cmd) ? "was YES -> NO" : "was NO");
    }
    return 0;
}

static void springboard_no_classic(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("SBApplication");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("isClassic")) : 0;
    if (!m) {
        j1_logstr("CLASSIC", "no -[SBApplication isClassic]");
        return;
    }
    orig_is_classic = (signed char (*)(id, SEL))get_imp(m);
    method_setImplementation(m, (IMP)is_classic_no);
}

/* Notes crash (run 102): CoreUI draws with a GPU CIContext; apps have no GL driver (no GL hooks outside backboardd),
 * so CoreImage calls through an empty GL table at address 0. In apps, force CoreImage's CPU renderer: every
 * CIContext factory gets kCIContextUseSoftwareRenderer = YES, EAGL-backed ones are replaced by CPU contexts. */
static id (*orig_ci_with_options)(id, SEL, id);
static id (*orig_ci_init_options)(id, SEL, id);
static id ci_software_options(id opts)
{
    id (*send0)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    id (*send1)(id, SEL, id) = (id (*)(id, SEL, id))objc_msgSend;
    void (*set)(id, SEL, id, id) = (void (*)(id, SEL, id, id))objc_msgSend;
    id (*str)(id, SEL, const char *) = (id (*)(id, SEL, const char *))objc_msgSend;
    id (*num)(id, SEL, signed char) = (id (*)(id, SEL, signed char))objc_msgSend;
    id md = opts ? send0(opts, sel_registerName("mutableCopy"))
                 : send0((id)objc_getClass("NSMutableDictionary"), sel_registerName("dictionary"));
    id key = str((id)objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "kCIContextUseSoftwareRenderer");
    static int logs;
    (void)send1;
    if (!md)
        return opts;
    set(md, sel_registerName("setObject:forKey:"), num((id)objc_getClass("NSNumber"), sel_registerName("numberWithBool:"), 1),
        key);
    if (logs++ < 4)
        j1_logstr("CIFIX", "software CIContext");
    return md;
}
static id ci_with_options(id self, SEL cmd, id opts)
{
    return orig_ci_with_options(self, cmd, ci_software_options(opts));
}
static id ci_with_eagl(id self, SEL cmd, id eagl)
{
    (void)cmd; (void)eagl;
    return orig_ci_with_options(self, sel_registerName("contextWithOptions:"), ci_software_options(0));
}
static id ci_with_eagl_options(id self, SEL cmd, id eagl, id opts)
{
    (void)cmd; (void)eagl;
    return orig_ci_with_options(self, sel_registerName("contextWithOptions:"), ci_software_options(opts));
}
static id ci_init_options(id self, SEL cmd, id opts)
{
    return orig_ci_init_options(self, cmd, ci_software_options(opts));
}

static void app_coreimage_software(void)
{
    Method (*get_cmethod)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getClassMethod");
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("CIContext");
    Method m;
    if (!c || !get_cmethod || !get_method || !get_imp) {
        j1_logstr("CIFIX", "no CIContext");
        return;
    }
    m = get_cmethod(c, sel_registerName("contextWithOptions:"));
    if (!m) {
        j1_logstr("CIFIX", "no +[CIContext contextWithOptions:]");
        return;
    }
    orig_ci_with_options = (id (*)(id, SEL, id))get_imp(m);
    method_setImplementation(m, (IMP)ci_with_options);
    if ((m = get_cmethod(c, sel_registerName("contextWithEAGLContext:"))))
        method_setImplementation(m, (IMP)ci_with_eagl);
    if ((m = get_cmethod(c, sel_registerName("contextWithEAGLContext:options:"))))
        method_setImplementation(m, (IMP)ci_with_eagl_options);
    if ((m = get_method(c, sel_registerName("initWithOptions:")))) {
        orig_ci_init_options = (id (*)(id, SEL, id))get_imp(m);
        method_setImplementation(m, (IMP)ci_init_options);
    }
}

/* Apps have no GL driver: -[EAGLContext initWithAPI:(sharegroup:)] returns nil so callers take their no-GPU path
 * instead of drawing through an empty GL table (Notes, run 102-103). */
static id eagl_init_nil(id self, SEL cmd, ...)
{
    static int logs;
    (void)self;
    if (logs++ < 4)
        j1_logstr("EAGLNIL", sel_getName(cmd));
    return 0;
}

static void app_no_eagl(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    Class c = objc_getClass("EAGLContext");
    Method m;
    if (!c || !get_method)
        return;
    if ((m = get_method(c, sel_registerName("initWithAPI:"))))
        method_setImplementation(m, (IMP)eagl_init_nil);
    if ((m = get_method(c, sel_registerName("initWithAPI:sharegroup:"))))
        method_setImplementation(m, (IMP)eagl_init_nil);
    /* run 105: CoreImage did not go through the two public initializers (no EAGLNIL logged) */
    if ((m = get_method(c, sel_registerName("initWithAPI:properties:"))))
        method_setImplementation(m, (IMP)eagl_init_nil);
    if ((m = get_method(c, sel_registerName("initWithAPI:sharedWithCompute:"))))
        method_setImplementation(m, (IMP)eagl_init_nil);
}

/* Photos (run 105): assetsd raises "Error validating url for store" from Core Data: log the store URLs it adds. */
static id (*orig_add_store)(id, SEL, id, id, id, id, id *);
static id add_store_log(id self, SEL cmd, id type, id conf, id url, id opts, id *err)
{
    static int logs;
    id r;
    const char *(*utf8)(id, SEL) = (const char *(*)(id, SEL))objc_msgSend;
    id (*send0)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
    if (logs < 10) {
        id d = url ? send0(url, sel_registerName("description")) : 0;
        log_cstr("PSTORE-URL", d ? utf8(d, sel_registerName("UTF8String")) : "(nil)");
    }
    r = orig_add_store(self, cmd, type, conf, url, opts, err);
    if (logs < 10) {
        logs++;
        if (!r && err && *err) {
            id d = send0(*err, sel_registerName("description"));
            log_cstr("PSTORE-ERR", d ? utf8(d, sel_registerName("UTF8String")) : "?");
        } else
            log_cstr("PSTORE", r ? "added" : "failed, no error");
    }
    return r;
}

static void assetsd_store_log(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("NSPersistentStoreCoordinator");
    Method m = (c && get_method && get_imp)
                   ? get_method(c, sel_registerName("addPersistentStoreWithType:configuration:URL:options:error:"))
                   : 0;
    if (!m) {
        j1_logstr("PSTORE", "no NSPersistentStoreCoordinator");
        return;
    }
    orig_add_store = (id (*)(id, SEL, id, id, id, id, id *))get_imp(m);
    method_setImplementation(m, (IMP)add_store_log);
}

/* The logger writes to stderr; an app's stderr may be a pipe with no reader: no SIGPIPE on it. */
static void stderr_no_sigpipe(void)
{
    int (*fc)(int, int, ...) = (int (*)(int, int, ...))dlsym((void *)-2, "fcntl");
    if (fc)
        fc(2, 73 /* F_SETNOSIGPIPE */, 1);
}

static int is_app_process(void)
{
    const char *(*image_name)(unsigned) = (const char *(*)(unsigned))dlsym((void *)-2, "_dyld_get_image_name");
    const char *n = image_name ? image_name(0) : 0;
    return n && (starts(n, "/Applications/") || starts(n, "/private/var/mobile/Applications/") ||
                 starts(n, "/var/mobile/Applications/"));
}

/* Home button: the kernel polls the S5's Home key (PMIC GPIO over SPMI) and counts presses in the "J1HomePresses"
 * property of the touch service (IOS7J1Touch). A SpringBoard thread watches it every 100 ms and, for each new press,
 * calls -[SBUIController clickedMenuButton] on the main thread (go home / close the app). */
static void *home_button_thread(void *arg)
{
    unsigned (*matching_svc)(unsigned, void *) = (unsigned (*)(unsigned, void *))dlsym((void *)-2, "IOServiceGetMatchingService");
    void *(*name_matching)(const char *) = (void *(*)(const char *))dlsym((void *)-2, "IOServiceNameMatching");
    void *(*copy_prop)(unsigned, void *, void *, unsigned) = (void *(*)(unsigned, void *, void *, unsigned))dlsym((void *)-2, "IORegistryEntryCreateCFProperty");
    void *(*cfstr)(void *, const char *, unsigned) = (void *(*)(void *, const char *, unsigned))dlsym((void *)-2, "CFStringCreateWithCString");
    int (*getval)(void *, int, void *) = (int (*)(void *, int, void *))dlsym((void *)-2, "CFNumberGetValue");
    void (*cfrelease)(void *) = (void (*)(void *))dlsym((void *)-2, "CFRelease");
    int (*usleep_fn)(unsigned) = (int (*)(unsigned))dlsym((void *)-2, "usleep");
    unsigned svc = 0, last = 0;
    int have_last = 0;
    void *key;
    (void)arg;
    if (!matching_svc || !name_matching || !copy_prop || !cfstr || !getval || !cfrelease || !usleep_fn)
        return 0;
    key = cfstr(0, "J1HomePresses", 0x08000100);
    for (;;) {
        void *v;
        usleep_fn(100000);
        if (!svc) {
            svc = matching_svc(0, name_matching("IOS7J1Touch"));
            if (!svc)
                continue;
            j1_logstr("HOMEBTN", "watching IOS7J1Touch J1HomePresses");
        }
        v = copy_prop(svc, key, 0, 0);
        if (v) {
            int n = 0;
            getval(v, 9 /* kCFNumberIntType */, &n);
            cfrelease(v);
            if (have_last && (unsigned)n != last) {
                id ctl = (id)objc_getClass("SBUIController");
                id (*send)(id, SEL) = (id (*)(id, SEL))objc_msgSend;
                void (*perform)(id, SEL, SEL, id, signed char) = (void (*)(id, SEL, SEL, id, signed char))objc_msgSend;
                id shared = ctl ? send(ctl, sel_registerName("sharedInstance")) : 0;
                j1_logstr("HOMEBTN", "press -> clickedMenuButton");
                if (shared)
                    perform(shared, sel_registerName("performSelectorOnMainThread:withObject:waitUntilDone:"),
                            sel_registerName("clickedMenuButton"), 0, 0);
            }
            last = (unsigned)n;
            have_last = 1;
        }
    }
    return 0;
}

static void springboard_home_button(void)
{
    int (*create)(void *, void *, void *(*)(void *), void *) = (int (*)(void *, void *, void *(*)(void *), void *))dlsym((void *)-2, "pthread_create");
    static unsigned char thr[64];
    if (create)
        create(thr, 0, home_button_thread, 0);
}

static void springboard_not_bricked(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("SBLockdownManager");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("brickedDevice")) : 0;
    Method ls = (c && get_method && get_imp) ? get_method(c, sel_registerName("lockdownState")) : 0;
    if (!m) {
        j1_logstr("BUDDY", "no -[SBLockdownManager brickedDevice]");
        return;
    }
    if (ls)
        orig_lockdown_state = (int (*)(id, SEL))get_imp(ls);
    orig_bricked = (signed char (*)(id, SEL))get_imp(m);
    method_setImplementation(m, (IMP)bricked_no);
}

static void springboard_setup_done(void)
{
    long (*slide_fn)(unsigned) = (long (*)(unsigned))dlsym((void *)-2, "_dyld_get_image_vmaddr_slide");
    long slide = slide_fn ? slide_fn(0) : 0; /* image 0 = the main executable (SpringBoard) */
    void **copy_slot = (void **)(0x329cfc + slide), **bool_slot = (void **)(0x329d04 + slide);
    real_copy_app_value = (void *(*)(void *, void *))dlsym((void *)-2, "CFPreferencesCopyAppValue");
    real_get_app_bool = (unsigned char (*)(void *, void *, unsigned char *))dlsym((void *)-2, "CFPreferencesGetAppBooleanValue");
    cf_getcstr = (int (*)(void *, char *, int, unsigned))dlsym((void *)-2, "CFStringGetCString");
    if (!slide_fn || !real_copy_app_value || !real_get_app_bool || !cf_getcstr) {
        j1_logstr("BUDDY", "setup override: missing symbols");
        return;
    }
    *copy_slot = (void *)copy_app_value_fix;
    *bool_slot = (void *)get_app_bool_fix;
    j1_logstr("BUDDY", "setup override installed");
}

static void springboard_exception_log(void)
{
    Method (*get_method)(Class, SEL) = (Method (*)(Class, SEL))dlsym((void *)-2, "class_getInstanceMethod");
    IMP (*get_imp)(Method) = (IMP (*)(Method))dlsym((void *)-2, "method_getImplementation");
    Class c = objc_getClass("NSException");
    Method m = (c && get_method && get_imp) ? get_method(c, sel_registerName("initWithName:reason:userInfo:")) : 0;
    install_exc_handler();
    if (!m) {
        j1_logstr("EXCNEW", "no -[NSException initWithName:reason:userInfo:]");
        return;
    }
    orig_exc_init = (id (*)(id, SEL, id, id, id))get_imp(m);
    method_setImplementation(m, (IMP)exc_init_log);
    j1_logstr("EXCNEW", "exception logger installed");
}

static int is_backboardd(void)
{
    const char *(*progname)(void) = (const char *(*)(void))dlsym((void *)-2, "getprogname");
    const char *p = progname ? progname() : 0;
    return p && starts(p, "backboardd");
}

__attribute__((constructor)) static void eagl_hook(void)
{
    Class c;
    {
        const char *(*progname)(void) = (const char *(*)(void))dlsym((void *)-2, "getprogname");
        const char *p = progname ? progname() : 0;
        if (p && starts(p, "assetsd")) {
            /* S5 run 100: assetsd aborts on Objective-C exceptions: log them, hook nothing else. */
            stderr_no_sigpipe();
            springboard_exception_log();
            assetsd_store_log();
            return;
        }
        if (p && !starts(p, "SpringBoard") && is_app_process()) {
            /* S5 run 102: apps get the exception logger and the ~ipad image / strings fallbacks, no GL hooks. */
            stderr_no_sigpipe();
            springboard_exception_log();
            springboard_ipad_icon_fallback();
            app_ipad_strings_fallback();
            app_coreimage_software();
            j1_logstr("APPSHIM", p);
            /* run 104 gave apps the software EAGL/GL (goto eagl): Settings then hit the QuartzCore slab assert.
             * Back to no GL in apps; EAGLContext creation fails instead (Notes: CoreUI/CoreImage). */
            app_no_eagl();
            return;
        }
        if (p && starts(p, "SpringBoard")) {
            stderr_no_sigpipe(); /* run 121: SpringBoard died of SIGPIPE writing our log to its stderr */
            springboard_exception_log();
            fix_screen_scale();
            fix_screen_geometry();
#ifndef J1_SB_ICON_ANIM_OFF
#define J1_SB_ICON_ANIM_OFF 1
#endif
#if J1_SB_ICON_ANIM_OFF
            /* Added in run 91 when there were no icons (zoom animator divided by an icon count of 0). With installd
             * back there are icons; build with J1_CFLAGS=-DJ1_SB_ICON_ANIM_OFF=0 to test SpringBoard without these
             * overrides (suspected in the run 99 heap corruption). */
            springboard_no_unscatter_animation();
            springboard_no_scatter_animation();
#endif
            springboard_setup_done();
            springboard_not_bricked();
            springboard_no_setup_app();
            springboard_log_events();
            springboard_ipad_icon_fallback();
            springboard_no_classic();
            springboard_home_button();
            springboard_wait_for_display();
            log_mg_screen();
            return; /* no GL hooks outside backboardd */
        }
    }
    if (is_backboardd()) {
        wait_for_framebuffer();
        hook_display_lookup();
        hook_main_display();
    }
    c = objc_getClass("EAGLContext");
    if (!c) {
        j1_logstr("EAGL", "class not found");
        return;
    }
    g_ctx[0x10 / 4] = (unsigned)g_ctx;
    j1_fill_ctx(g_ctx);
    hook_class(c);
    hook_class(object_getClass((id)c));
    install_exc_handler();
    j1_logstr("EAGL", "hooked");
}
