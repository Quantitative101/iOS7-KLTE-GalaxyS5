/* Software rasterizer behind the fake GLES2 macro context. Included by impl.c only (gen-ctx.py scans impl.c for j1i_ names).
 * Supported: FBO colour attachments (texture or renderbuffer), clear, scissor, blend, indexed/array triangle draws with the
 * CoreAnimation uber-shader semantics:  gl_Position = vertex_matrix * vertex_position;  color0 = vertex_color0;
 * texcoord0_0 = vertex_texcoord0 * texmat0.xy + texmat0.zw;  frag = [texture2D(tex0, texcoord0_0) *] color0.
 * Pixels are stored as A<<24|R<<16|G<<8|B (BGRA bytes in memory, the IOSurface 'BGRA' layout), row 0 = GL y 0. */

typedef unsigned int u32;
typedef int s32;
typedef unsigned char u8;
typedef unsigned short u16;
typedef __SIZE_TYPE__ usize;

#define R_MAXOBJ 512
#define R_HEAP (24u << 20)

static u8 r_heap[R_HEAP] __attribute__((aligned(16)));
static u32 r_heap_top;

static void *r_alloc(u32 n)
{
    void *p;
    n = (n + 15u) & ~15u;
    if (r_heap_top + n > R_HEAP)
        return 0;
    p = r_heap + r_heap_top;
    r_heap_top += n;
    return p;
}

static void r_copy(void *d, const void *s, u32 n)
{
    u8 *dd = (u8 *)d;
    const u8 *ss = (const u8 *)s;
    while (n--)
        *dd++ = *ss++;
}

typedef struct { u8 *p; u32 size, cap; } r_buf_t;
typedef struct { u32 *px; s32 w, h, stride; u8 external; } r_surf_t; /* texture or renderbuffer storage */
typedef struct { u32 color_tex, color_rb; } r_fbo_t;
typedef struct { u8 enabled, norm, cvset; s32 size, type, stride; const u8 *ptr; u32 buf; float cv[4]; } r_attr_t;
typedef struct { u32 prog, loc; float f[16]; } r_uni_t;
/* Fragment modes, picked from CA's fragment shader source (impl.c j1i_glShaderSource):
 *   R_FM_COLOR  gl_FragData[0] = color0
 *   R_FM_MOD    color0 * s0          (also the blend-mode shaders, approximated as normal blending)
 *   R_FM_TEX    s0                   (also the blur, approximated by its centre tap)
 *   R_FM_MASK   color0 * s0 * s1.a   (glyphs masked by a second texture: "slide to unlock" shimmer)
 *   R_FM_C1A    color0 * color1.a */
#define R_FM_COLOR 0
#define R_FM_MOD 1
#define R_FM_TEX 2
#define R_FM_MASK 3
#define R_FM_C1A 4
typedef struct { s32 pos, col, tc0, col1, tc1; u32 frag_tex; } r_prog_t;

static r_buf_t r_bufs[R_MAXOBJ];
static r_surf_t r_tex[R_MAXOBJ], r_rb[R_MAXOBJ];
static r_fbo_t r_fbo[R_MAXOBJ];
static r_prog_t r_prog[R_MAXOBJ];
static u32 r_shader_tex[R_MAXOBJ];     /* shader id -> source contains texture2D */
static u32 r_prog_frag[R_MAXOBJ];      /* program id -> fragment shader id */
static r_attr_t r_attr[16];
/* Uniform values are per program, as in GL: CA only re-sends a uniform when its value changed for that program, so one
 * global table (the old 48-entry one keyed by name) handed draws another program's texmat0/vertex_matrix (S5 run 67:
 * sheared, squashed, hatched text). Open-addressed on (program, location). */
#define R_MAXUNI 2048
static r_uni_t r_uni[R_MAXUNI];
static u32 r_nuni;
static u32 r_bound_array, r_bound_element, r_bound_tex[8], r_active_unit;
static u32 r_cur_fbo, r_cur_rb, r_cur_prog;
static s32 r_viewport[4] = {0, 0, 480, 800};
static s32 r_scissor[4] = {0, 0, 480, 800};
static u32 r_scissor_on, r_blend_on, r_sfac = 1, r_dfac = 0;
static float r_clear[4];
static u32 r_draws, r_clears, r_draw_hits;

/* Profiler (run 107): mach_absolute_time ticks (24 MHz on the S5) spent per phase, summed over a 32-frame window and
 * logged as PROF by j1_log_frame. No division on the device (no compiler runtime): raw tick sums are logged. */
extern void *dlsym(void *, const char *); /* the device SDK stubs lack mach_absolute_time: resolve it once */
static unsigned long long (*r_mat)(void);
static inline u32 r_now(void)
{
    if (!r_mat)
        r_mat = (unsigned long long (*)(void))dlsym((void *)-2, "mach_absolute_time");
    return r_mat ? (u32)r_mat() : 0;
}
enum { P_DRAW, P_RECT, P_RECTNO, P_TRI, P_CLEAR, P_TEX, P_READ, P_SETUP, P_N };
/* run 120: why r_rect_impl rejects a quad (counts per reason, logged as RREJ with PROF) */
static u32 r_rej[16];
#define R_REJ(n) do { r_rej[(n) & 15]++; return 0; } while (0)
static u32 r_prof[P_N], r_prof_rects, r_prof_tris, r_prof_rectpx, r_prof_clearpx, r_prof_texpx;
/* Backing of the default framebuffer (fbo 0): the surface most recently attached with attachImage:toCoreSurface:. */
static r_surf_t r_default;

static u32 r_uni_hash(const char *name)
{
    u32 h = 5381;
    for (const char *s = name; s && *s; s++)
        h = h * 33 + (u8)*s;
    return (h & 0xFFFFu) + 1;
}

static r_uni_t *r_uni_find(u32 loc, int create)
{
    u32 prog = r_cur_prog, h = (prog * 2654435761u ^ loc * 40503u) & (R_MAXUNI - 1);
    for (u32 probe = 0; probe < R_MAXUNI; probe++, h = (h + 1) & (R_MAXUNI - 1)) {
        r_uni_t *u = &r_uni[h];
        if (u->loc == 0) {          /* empty slot: the uniform was never set for this program */
            if (!create || r_nuni >= R_MAXUNI - 1)
                return 0;
            u->prog = prog;
            u->loc = loc;
            for (int i = 0; i < 16; i++)
                u->f[i] = 0;
            r_nuni++;
            return u;
        }
        if (u->loc == loc && u->prog == prog)
            return u;
    }
    return 0;
}

static int r_has(const char *s, s32 n, const char *pat)
{
    s32 pl = 0;
    while (pat[pl])
        pl++;
    for (s32 i = 0; i + pl <= n; i++) {
        s32 k = 0;
        while (k < pl && s[i + k] == pat[k])
            k++;
        if (k == pl)
            return 1;
    }
    return 0;
}

/* Current colour target, or 0. */
static r_surf_t *r_target(void)
{
    if (!r_cur_fbo)
        return r_default.px ? &r_default : 0;
    if (r_cur_fbo >= R_MAXOBJ)
        return 0;
    r_fbo_t *f = &r_fbo[r_cur_fbo];
    if (f->color_tex && f->color_tex < R_MAXOBJ && r_tex[f->color_tex].px)
        return &r_tex[f->color_tex];
    if (f->color_rb && f->color_rb < R_MAXOBJ && r_rb[f->color_rb].px)
        return &r_rb[f->color_rb];
    return 0;
}

static float r_clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static u32 r_pack(float r, float g, float b, float a)
{
    return ((u32)(r_clamp01(a) * 255.0f + 0.5f) << 24) | ((u32)(r_clamp01(r) * 255.0f + 0.5f) << 16) |
           ((u32)(r_clamp01(g) * 255.0f + 0.5f) << 8) | (u32)(r_clamp01(b) * 255.0f + 0.5f);
}

static void r_scissor_rect(const r_surf_t *t, s32 *x0, s32 *y0, s32 *x1, s32 *y1)
{
    *x0 = 0; *y0 = 0; *x1 = t->w; *y1 = t->h;
    if (r_scissor_on) {
        if (r_scissor[0] > *x0) *x0 = r_scissor[0];
        if (r_scissor[1] > *y0) *y0 = r_scissor[1];
        if (r_scissor[0] + r_scissor[2] < *x1) *x1 = r_scissor[0] + r_scissor[2];
        if (r_scissor[1] + r_scissor[3] < *y1) *y1 = r_scissor[1] + r_scissor[3];
    }
}

static void r_do_clear(void)
{
    r_surf_t *t = r_target();
    s32 x0, y0, x1, y1;
    u32 v = r_pack(r_clear[0], r_clear[1], r_clear[2], r_clear[3]);
    r_clears++;
    if (!t)
        return;
    r_scissor_rect(t, &x0, &y0, &x1, &y1);
    {
        u32 t0 = r_now();
        for (s32 y = y0; y < y1; y++)
            for (s32 x = x0; x < x1; x++)
                t->px[y * t->stride + x] = v;
        r_prof[P_CLEAR] += r_now() - t0;
        if (x1 > x0 && y1 > y0)
            r_prof_clearpx += (u32)((x1 - x0) * (y1 - y0));
    }
}

static float r_factor(u32 f, float s, float d, float sa, float da, int comp_alpha)
{
    (void)comp_alpha;
    switch (f) {
    case 0: return 0;
    case 1: return 1;
    case 0x300: return s;
    case 0x301: return 1 - s;
    case 0x302: return sa;
    case 0x303: return 1 - sa;
    case 0x304: return da;
    case 0x305: return 1 - da;
    case 0x306: return d;
    case 0x307: return 1 - d;
    }
    return 1;
}

static u32 r_fast = 1; /* 0: reference paths only (the native benchmark compares both) */

/* The device build links without libm or the compiler runtime: small local replacements. Inputs are screen-space
 * coordinates (|v| far below 2^30). */
static s32 r_ceil(float f) { s32 i = (s32)f; return f > (float)i ? i + 1 : i; }
static long long r_round(float f) { return (long long)(s32)(f >= 0 ? f + 0.5f : f - 0.5f); }
static float r_lf(long long v) { return (float)(s32)(v >> 16) * 65536.0f + (float)(s32)(v & 0xffff); }
static int r_memeq(const void *a, const void *b, u32 n)
{
    const u8 *p = (const u8 *)a, *q = (const u8 *)b;
    for (u32 i = 0; i < n; i++)
        if (p[i] != q[i])
            return 0;
    return 1;
}

static void r_blend_px(u32 *dst, float r, float g, float b, float a)
{
    if (!r_blend_on) {
        *dst = r_pack(r, g, b, a);
        return;
    }
    u32 d = *dst;
    float da = ((d >> 24) & 255) / 255.0f, dr = ((d >> 16) & 255) / 255.0f, dg = ((d >> 8) & 255) / 255.0f,
          db = (d & 255) / 255.0f;
    /* CA's two common blend modes, same float operations as the general path below (exact) without the factor
     * switch per channel: premultiplied (ONE, ONE_MINUS_SRC_ALPHA) and (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). */
    if (r_fast && r_dfac == 0x303 && (r_sfac == 1 || r_sfac == 0x302)) {
        float sf = r_sfac == 1 ? 1.0f : a, inv = 1 - a;
        *dst = r_pack(r * sf + dr * inv, g * sf + dg * inv, b * sf + db * inv, a * (r_sfac == 1 ? 1.0f : a) + da * inv);
        return;
    }
    float sc[3] = {r, g, b}, dc[3] = {dr, dg, db}, o[3];
    for (int i = 0; i < 3; i++)
        o[i] = sc[i] * r_factor(r_sfac, sc[i], dc[i], a, da, 0) + dc[i] * r_factor(r_dfac, sc[i], dc[i], a, da, 0);
    float oa = a * r_factor(r_sfac, a, da, a, da, 1) + da * r_factor(r_dfac, a, da, a, da, 1);
    *dst = r_pack(o[0], o[1], o[2], oa);
}

static u32 r_sample(const r_surf_t *t, float u, float v, float *r, float *g, float *b, float *a)
{
    s32 x, y;
    u32 p;
    u -= (float)(s32)u - (u < 0 ? 1.0f : 0.0f);   /* REPEAT-style wrap; CA mostly stays in [0,1] */
    v -= (float)(s32)v - (v < 0 ? 1.0f : 0.0f);
    x = (s32)(u * t->w);
    y = (s32)(v * t->h);
    if (x >= t->w) x = t->w - 1;
    if (y >= t->h) y = t->h - 1;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    p = t->px[y * t->stride + x];
    *a = ((p >> 24) & 255) / 255.0f;
    *r = ((p >> 16) & 255) / 255.0f;
    *g = ((p >> 8) & 255) / 255.0f;
    *b = (p & 255) / 255.0f;
    return p;
}

/* Fetch attribute idx of vertex v as up to 4 floats (missing components: 0,0,0,1). */
static void r_fetch(s32 idx, u32 v, float out[4])
{
    const r_attr_t *a;
    out[0] = out[1] = out[2] = 0; out[3] = 1;
    if (idx < 0 || idx >= 16)
        return;
    a = &r_attr[idx];
    if (!a->enabled) {
        if (a->cvset) /* else the GL default (0, 0, 0, 1) set above */
            for (int i = 0; i < 4; i++) out[i] = a->cv[i];
        return;
    }
    const u8 *base = a->ptr;
    if (a->buf) {
        if (a->buf >= R_MAXOBJ || !r_bufs[a->buf].p)
            return;
        base = r_bufs[a->buf].p + (usize)a->ptr;
    }
    if (!base)
        return;
    {
        s32 esz = a->type == 0x1406 ? 4 : (a->type == 0x1403 || a->type == 0x1402) ? 2 : 1;
        s32 stride = a->stride ? a->stride : esz * a->size;
        const u8 *p = base + (usize)v * (u32)stride;
        for (s32 c = 0; c < a->size && c < 4; c++) {
            float f;
            if (a->type == 0x1406) {
                u32 raw = (u32)p[c * 4] | ((u32)p[c * 4 + 1] << 8) | ((u32)p[c * 4 + 2] << 16) | ((u32)p[c * 4 + 3] << 24);
                __builtin_memcpy(&f, &raw, 4);
            } else if (a->type == 0x1401) {
                f = p[c];
                if (a->norm) f /= 255.0f;
            } else if (a->type == 0x1402 || a->type == 0x1403) {
                u32 raw = (u32)p[c * 2] | ((u32)p[c * 2 + 1] << 8);
                f = a->type == 0x1402 ? (float)(s32)(raw & 0x8000u ? raw | 0xFFFF0000u : raw) : (float)raw;
                if (a->norm) f /= (a->type == 0x1402 ? 32767.0f : 65535.0f);
            } else
                f = 0;
            out[c] = f;
        }
    }
}

typedef struct { float x, y, iw; float c[4]; float c1[4]; float t[2]; float t1[2]; } r_vtx_t;

static void r_setup_vertex(u32 v, const r_prog_t *pg, const float *m, const float *tm, const float *tm1, r_vtx_t *o)
{
    float p[4], col[4], col1[4], tc[4];
    r_fetch(pg->pos, v, p);
    r_fetch(pg->col, v, col);
    r_fetch(pg->col1, v, col1);
    float cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12] * p[3];
    float cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13] * p[3];
    float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15] * p[3];
    float iw = cw != 0 ? 1.0f / cw : 0;
    o->x = r_viewport[0] + (cx * iw * 0.5f + 0.5f) * r_viewport[2];
    o->y = r_viewport[1] + (cy * iw * 0.5f + 0.5f) * r_viewport[3];
    o->iw = cw > 0 ? iw : 0;     /* iw == 0 marks an unclippable vertex behind the eye; the triangle is skipped */
    for (int i = 0; i < 4; i++) {
        o->c[i] = col[i];
        o->c1[i] = col1[i];
    }
    if (pg->tc0 >= 0) {
        r_fetch(pg->tc0, v, tc);
        o->t[0] = tc[0] * tm[0] + tm[2];
        o->t[1] = tc[1] * tm[1] + tm[3];
    } else
        o->t[0] = o->t[1] = 0;
    if (pg->tc1 >= 0) {
        r_fetch(pg->tc1, v, tc);
        o->t1[0] = tc[0] * tm1[0] + tm1[2];
        o->t1[1] = tc[1] * tm1[1] + tm1[3];
    } else
        o->t1[0] = o->t1[1] = 0;
}

static int r_owns(float dx, float dy) { return dy > 0 || (dy == 0 && dx < 0); }

static void r_tri_impl(const r_surf_t *t, const r_vtx_t *a, const r_vtx_t *b, const r_vtx_t *c, const r_surf_t *tex,
                  const r_surf_t *tex1, u32 fmode)
{
    s32 sx0, sy0, sx1, sy1;
    float minx = a->x, maxx = a->x, miny = a->y, maxy = a->y;
    float area;
    if (a->iw == 0 || b->iw == 0 || c->iw == 0)
        return;
    if (b->x < minx) minx = b->x;
    if (c->x < minx) minx = c->x;
    if (b->x > maxx) maxx = b->x;
    if (c->x > maxx) maxx = c->x;
    if (b->y < miny) miny = b->y;
    if (c->y < miny) miny = c->y;
    if (b->y > maxy) maxy = b->y;
    if (c->y > maxy) maxy = c->y;
    r_scissor_rect(t, &sx0, &sy0, &sx1, &sy1);
    {
        s32 bx0 = (s32)(minx < 0 ? 0 : minx), by0 = (s32)(miny < 0 ? 0 : miny);
        s32 bx1 = (s32)(maxx + 1.0f), by1 = (s32)(maxy + 1.0f);
        if (bx0 < sx0) bx0 = sx0;
        if (by0 < sy0) by0 = sy0;
        if (bx1 > sx1) bx1 = sx1;
        if (by1 > sy1) by1 = sy1;
        /* Coverage in fixed point (8 sub-pixel bits, 64-bit edge functions): exact, so a pixel centre on an edge
         * shared by two triangles is owned by exactly one of them (the float version double-blended or dropped such
         * pixels along every quad diagonal). Edge functions step incrementally across the box. */
        long long ax = r_round(a->x * 256.0f), ay = r_round(a->y * 256.0f);
        long long bxf = r_round(b->x * 256.0f), byf = r_round(b->y * 256.0f);
        long long cxf = r_round(c->x * 256.0f), cyf = r_round(c->y * 256.0f);
        long long iarea = (bxf - ax) * (cyf - ay) - (cxf - ax) * (byf - ay);
        if (iarea == 0)
            return;
        if (iarea < 0) {        /* normalise winding so the fill rule below is consistent between neighbours */
            const r_vtx_t *tmp = b;
            long long tx = bxf, ty = byf;
            b = c;
            c = tmp;
            bxf = cxf; byf = cyf;
            cxf = tx; cyf = ty;
            iarea = -iarea;
        }
        area = r_lf(iarea);
        {
            /* edge e0 = b->c, e1 = c->a, e2 = a->b; value at P: (B.x-P.x)(C.y-P.y) - (C.x-P.x)(B.y-P.y) */
            long long px0 = (long long)bx0 * 256 + 128, py0 = (long long)by0 * 256 + 128;
            long long e0r = (bxf - px0) * (cyf - py0) - (cxf - px0) * (byf - py0);
            long long e1r = (cxf - px0) * (ay - py0) - (ax - px0) * (cyf - py0);
            long long e2r = (ax - px0) * (byf - py0) - (bxf - px0) * (ay - py0);
            long long e0dx = (byf - cyf) * 256, e1dx = (cyf - ay) * 256, e2dx = (ay - byf) * 256;
            long long e0dy = (cxf - bxf) * 256, e1dy = (ax - cxf) * 256, e2dy = (bxf - ax) * 256;
            int own0 = r_owns((float)(s32)(cxf - bxf), (float)(s32)(cyf - byf)), own1 = r_owns((float)(s32)(ax - cxf), (float)(s32)(ay - cyf));
            int own2 = r_owns((float)(s32)(bxf - ax), (float)(s32)(byf - ay));
        for (s32 y = by0; y < by1; y++, e0r += e0dy, e1r += e1dy, e2r += e2dy) {
            long long e0 = e0r, e1 = e1r, e2 = e2r;
            for (s32 x = bx0; x < bx1; x++, e0 += e0dx, e1 += e1dx, e2 += e2dx) {
                float w0, w1, w2;
                if (e0 < 0 || e1 < 0 || e2 < 0)
                    continue;
                /* a pixel centre exactly on an edge belongs to only one of the two triangles sharing it */
                if ((e0 == 0 && !own0) || (e1 == 0 && !own1) || (e2 == 0 && !own2))
                    continue;
                w0 = r_lf(e0) / area;
                w1 = r_lf(e1) / area;
                w2 = r_lf(e2) / area;
                {
                    float q = w0 * a->iw + w1 * b->iw + w2 * c->iw;
                    float k0 = w0 * a->iw / q, k1 = w1 * b->iw / q, k2 = w2 * c->iw / q;
                    float col[4], tr, tg, tb, ta;
                    for (int i = 0; i < 4; i++)
                        col[i] = r_clamp01(k0 * a->c[i] + k1 * b->c[i] + k2 * c->c[i]);
                    if (tex && fmode != R_FM_COLOR && fmode != R_FM_C1A) {
                        r_sample(tex, k0 * a->t[0] + k1 * b->t[0] + k2 * c->t[0],
                                 k0 * a->t[1] + k1 * b->t[1] + k2 * c->t[1], &tr, &tg, &tb, &ta);
                        if (fmode == R_FM_TEX) {
                            col[0] = tr; col[1] = tg; col[2] = tb; col[3] = ta;
                        } else {
                            col[0] *= tr; col[1] *= tg; col[2] *= tb; col[3] *= ta;
                        }
                    }
                    if (fmode == R_FM_MASK && tex1) {
                        float mr, mg, mb, ma;
                        r_sample(tex1, k0 * a->t1[0] + k1 * b->t1[0] + k2 * c->t1[0],
                                 k0 * a->t1[1] + k1 * b->t1[1] + k2 * c->t1[1], &mr, &mg, &mb, &ma);
                        for (int i = 0; i < 4; i++)
                            col[i] *= ma;
                    } else if (fmode == R_FM_C1A) {
                        float a1 = r_clamp01(k0 * a->c1[3] + k1 * b->c1[3] + k2 * c->c1[3]);
                        for (int i = 0; i < 4; i++)
                            col[i] *= a1;
                    }
                    r_blend_px(&t->px[y * t->stride + x], col[0], col[1], col[2], col[3]);
                }
            }
        }
        }
    }
}

/* Texel index along one axis, exactly as r_sample computes it (wrap, scale, clamp). */
static s32 r_texel_index(float u, s32 n)
{
    s32 x;
    u -= (float)(s32)u - (u < 0 ? 1.0f : 0.0f);
    x = (s32)(u * n);
    if (x >= n) x = n - 1;
    if (x < 0) x = 0;
    return x;
}

static int r_same_vtx(const r_vtx_t *a, const r_vtx_t *b) { return r_memeq(a, b, sizeof *a); }

/* Fast path for what CoreAnimation draws most: two triangles (or a 4-vertex strip) tiling an axis-aligned rectangle
 * with an affine projection (equal w), flat colour and texture coordinates that each vary along one axis only.
 * Pixel coverage (centre inside, left/top edge inclusive), texel choice (r_texel_index) and the shading arithmetic
 * match r_tri; only the interpolated coordinate is computed directly instead of from barycentrics, so a texel at an
 * exact boundary can rarely differ by one. Returns 0 (nothing drawn) when the vertices do not qualify.
 * tri: the two triangles as 6 vertices (a,b,c, d,e,f). */
static int r_rect_impl(const r_surf_t *t, const r_vtx_t *tri, const r_surf_t *tex, const r_surf_t *tex1, u32 fmode)
{
    const r_vtx_t *corner[4] = {0, 0, 0, 0}; /* index: (x == x1) | (y == y1) << 1 */
    float xs[2], ys[2];
    int nx = 0, ny = 0, shared = 0;
    static s32 colx[4096], coly[4096], colx1[4096], coly1[4096];
    s32 bx0, bx1, by0, by1, sx0, sy0, sx1, sy1;
    float col0[4], c1a;
    int use_tex, use_mask;

    if (!r_fast || tri[0].iw == 0)
        R_REJ(1);
    for (int i = 0; i < 6; i++) {
        int fx = -1, fy = -1;
        if (tri[i].iw != tri[0].iw)
            R_REJ(2);
        for (int k = 0; k < nx; k++) if (xs[k] == tri[i].x) fx = k;
        for (int k = 0; k < ny; k++) if (ys[k] == tri[i].y) fy = k;
        if (fx < 0) { if (nx == 2) R_REJ(3); xs[nx] = tri[i].x; fx = nx++; }
        if (fy < 0) { if (ny == 2) R_REJ(4); ys[ny] = tri[i].y; fy = ny++; }
    }
    if (nx != 2 || ny != 2)
        R_REJ(5);
    if (xs[0] > xs[1]) { float s = xs[0]; xs[0] = xs[1]; xs[1] = s; }
    if (ys[0] > ys[1]) { float s = ys[0]; ys[0] = ys[1]; ys[1] = s; }
    for (int i = 0; i < 6; i++) {
        int k = (tri[i].x == xs[1]) | (tri[i].y == ys[1]) << 1;
        if (!corner[k])
            corner[k] = &tri[i];
        else if (!r_same_vtx(corner[k], &tri[i]))
            R_REJ(6); /* same corner, different attributes */
    }
    for (int k = 0; k < 4; k++)
        if (!corner[k])
            R_REJ(7);
    /* each triangle must use 3 different corners, and the two must share the diagonal (else they overlap) */
    for (int tr = 0; tr < 2; tr++) {
        int seen = 0;
        for (int j = 0; j < 3; j++) {
            const r_vtx_t *p = &tri[tr * 3 + j];
            int k = (p->x == xs[1]) | (p->y == ys[1]) << 1;
            if (seen & (1 << k))
                R_REJ(8);
            seen |= 1 << k;
        }
        shared = tr ? (shared & seen) : seen;
    }
    if (shared != 0x9 && shared != 0x6) /* corners 0+3 or 1+2: a diagonal */
        R_REJ(9);
    /* flat colour, texcoords separable: u from x only, v from y only */
    for (int k = 1; k < 4; k++)
        /* run 121: CA's quads have identical colours but corner 0's second-colour alpha differs (0.125 vs 0); that
         * value is only used in R_FM_C1A mode, so compare it only there */
        if (!r_memeq(corner[k]->c, corner[0]->c, sizeof corner[0]->c) ||
            (fmode == R_FM_C1A && corner[k]->c1[3] != corner[0]->c1[3])) {
            /* run 121: log what differs (first 12): fmode, prog, then per corner colour as 0xRRGGBBAA (x255) and
             * c1.a x1000, corners in index order (x==x1)|(y==y1)<<1 */
            static int rlogs;
            if (rlogs < 12) {
                extern void j1_logbuf(const char *tag, const char *s, int n);
                char line[160];
                int n = 0;
                u32 vals[10];
                rlogs++;
                vals[0] = fmode << 16 | r_cur_prog;
                vals[1] = (u32)(s32)(xs[1] - xs[0]) << 16 | (u32)(s32)(ys[1] - ys[0]);
                for (int q = 0; q < 4; q++) {
                    const float *c = corner[q]->c;
                    vals[2 + q * 2] = (u32)(s32)(c[0] * 255) << 24 | ((u32)(s32)(c[1] * 255) & 255) << 16 |
                                      ((u32)(s32)(c[2] * 255) & 255) << 8 | ((u32)(s32)(c[3] * 255) & 255);
                    vals[3 + q * 2] = (u32)(s32)(corner[q]->c1[3] * 1000);
                }
                for (int i = 0; i < 10; i++) {
                    for (int sh = 28; sh >= 0; sh -= 4)
                        line[n++] = "0123456789abcdef"[(vals[i] >> sh) & 15];
                    line[n++] = ' ';
                }
                j1_logbuf("RREJC", line, n);
            }
            R_REJ(10);
        }
    /* run 122: like c1 (run 121), only compare the coordinates the fragment mode actually samples */
    if ((tex && fmode != R_FM_COLOR && fmode != R_FM_C1A &&
         (corner[0]->t[0] != corner[2]->t[0] || corner[1]->t[0] != corner[3]->t[0] ||
          corner[0]->t[1] != corner[1]->t[1] || corner[2]->t[1] != corner[3]->t[1])) ||
        (fmode == R_FM_MASK && tex1 &&
         (corner[0]->t1[0] != corner[2]->t1[0] || corner[1]->t1[0] != corner[3]->t1[0] ||
          corner[0]->t1[1] != corner[1]->t1[1] || corner[2]->t1[1] != corner[3]->t1[1])))
        R_REJ(11);

    r_scissor_rect(t, &sx0, &sy0, &sx1, &sy1);
    bx0 = r_ceil(xs[0] - 0.5f); bx1 = r_ceil(xs[1] - 0.5f);
    by0 = r_ceil(ys[0] - 0.5f); by1 = r_ceil(ys[1] - 0.5f);
    if (bx0 < sx0) bx0 = sx0;
    if (by0 < sy0) by0 = sy0;
    if (bx1 > sx1) bx1 = sx1;
    if (by1 > sy1) by1 = sy1;
    if (bx0 >= bx1 || by0 >= by1)
        return 1;
    if (bx1 - bx0 > 4096 || by1 - by0 > 4096)
        R_REJ(12);
    use_tex = tex && fmode != R_FM_COLOR && fmode != R_FM_C1A;
    use_mask = fmode == R_FM_MASK && tex1;
    {
        float ix = 1.0f / (xs[1] - xs[0]), iy = 1.0f / (ys[1] - ys[0]);
        for (s32 x = bx0; x < bx1; x++) {
            float f = (x + 0.5f - xs[0]) * ix;
            if (use_tex) colx[x - bx0] = r_texel_index(corner[0]->t[0] + f * (corner[1]->t[0] - corner[0]->t[0]), tex->w);
            if (use_mask) colx1[x - bx0] = r_texel_index(corner[0]->t1[0] + f * (corner[1]->t1[0] - corner[0]->t1[0]), tex1->w);
        }
        for (s32 y = by0; y < by1; y++) {
            float f = (y + 0.5f - ys[0]) * iy;
            if (use_tex) coly[y - by0] = r_texel_index(corner[0]->t[1] + f * (corner[2]->t[1] - corner[0]->t[1]), tex->h);
            if (use_mask) coly1[y - by0] = r_texel_index(corner[0]->t1[1] + f * (corner[2]->t1[1] - corner[0]->t1[1]), tex1->h);
        }
    }
    for (int i = 0; i < 4; i++)
        col0[i] = r_clamp01(corner[0]->c[i]);
    c1a = r_clamp01(corner[0]->c1[3]);
    /* Integer pipelines for the two dominant cases (no mask, no colour1 alpha):
     *  - blending off and the texel used as-is (TEX, or MOD by white): the float path reduces exactly to a copy;
     *  - premultiplied blend (ONE, ONE_MINUS_SRC_ALPHA): source = texel x colour, dst = src + dst x (255 - a) / 255,
     *    rounded; within 1 of the float path. */
    if (!use_mask && fmode != R_FM_C1A) {
        int white = col0[0] == 1 && col0[1] == 1 && col0[2] == 1 && col0[3] == 1;
        u32 ci[4];
        for (int i = 0; i < 4; i++)
            ci[i] = (u32)(col0[i] * 255.0f + 0.5f);
        if (!r_blend_on && !use_tex) { /* opaque flat fill: r_blend_px would store r_pack(colour) everywhere */
            u32 v = r_pack(col0[0], col0[1], col0[2], col0[3]);
            for (s32 y = by0; y < by1; y++) {
                u32 *dst = &t->px[y * t->stride];
                for (s32 x = bx0; x < bx1; x++)
                    dst[x] = v;
            }
            return 1;
        }
        if (!r_blend_on && use_tex && (fmode == R_FM_TEX || white)) {
            for (s32 y = by0; y < by1; y++) {
                u32 *dst = &t->px[y * t->stride];
                const u32 *trow = &tex->px[coly[y - by0] * tex->stride];
                for (s32 x = bx0; x < bx1; x++)
                    dst[x] = trow[colx[x - bx0]];
            }
            return 1;
        }
        if (r_blend_on && r_sfac == 1 && r_dfac == 0x303) {
            for (s32 y = by0; y < by1; y++) {
                u32 *dst = &t->px[y * t->stride];
                const u32 *trow = use_tex ? &tex->px[coly[y - by0] * tex->stride] : 0;
                for (s32 x = bx0; x < bx1; x++) {
                    u32 s, d = dst[x], sa, sr, sg, sb, ia;
                    if (use_tex) {
                        s = trow[colx[x - bx0]];
                        if (fmode == R_FM_TEX || white) {
                            sa = s >> 24; sr = (s >> 16) & 255; sg = (s >> 8) & 255; sb = s & 255;
                        } else {
                            sa = ((s >> 24) * ci[3] + 128) * 257 >> 16;
                            sr = (((s >> 16) & 255) * ci[0] + 128) * 257 >> 16;
                            sg = (((s >> 8) & 255) * ci[1] + 128) * 257 >> 16;
                            sb = ((s & 255) * ci[2] + 128) * 257 >> 16;
                        }
                    } else {
                        sa = ci[3]; sr = ci[0]; sg = ci[1]; sb = ci[2];
                    }
                    if (sa == 255) {
                        dst[x] = sa << 24 | sr << 16 | sg << 8 | sb;
                        continue;
                    }
                    if ((sa | sr | sg | sb) == 0)
                        continue;
                    ia = 255 - sa;
                    {
                        u32 oa = sa + ((((d >> 24) * ia + 128) * 257) >> 16);
                        u32 orr = sr + (((((d >> 16) & 255) * ia + 128) * 257) >> 16);
                        u32 og = sg + (((((d >> 8) & 255) * ia + 128) * 257) >> 16);
                        u32 ob = sb + ((((d & 255) * ia + 128) * 257) >> 16);
                        if (oa > 255) oa = 255;
                        if (orr > 255) orr = 255;
                        if (og > 255) og = 255;
                        if (ob > 255) ob = 255;
                        dst[x] = oa << 24 | orr << 16 | og << 8 | ob;
                    }
                }
            }
            return 1;
        }
    }
    for (s32 y = by0; y < by1; y++) {
        u32 *dst = &t->px[y * t->stride];
        const u32 *trow = use_tex ? &tex->px[coly[y - by0] * tex->stride] : 0;
        const u32 *mrow = use_mask ? &tex1->px[coly1[y - by0] * tex1->stride] : 0;
        for (s32 x = bx0; x < bx1; x++) {
            float col[4] = {col0[0], col0[1], col0[2], col0[3]};
            if (use_tex) {
                u32 p = trow[colx[x - bx0]];
                float ta = ((p >> 24) & 255) / 255.0f, tr = ((p >> 16) & 255) / 255.0f, tg = ((p >> 8) & 255) / 255.0f,
                      tb = (p & 255) / 255.0f;
                if (fmode == R_FM_TEX) {
                    col[0] = tr; col[1] = tg; col[2] = tb; col[3] = ta;
                } else {
                    col[0] *= tr; col[1] *= tg; col[2] *= tb; col[3] *= ta;
                }
            }
            if (use_mask) {
                float ma = ((mrow[colx1[x - bx0]] >> 24) & 255) / 255.0f;
                for (int i = 0; i < 4; i++)
                    col[i] *= ma;
            } else if (fmode == R_FM_C1A) {
                for (int i = 0; i < 4; i++)
                    col[i] *= c1a;
            }
            r_blend_px(&dst[x], col[0], col[1], col[2], col[3]);
        }
    }
    return 1;
}

static u32 r_index(const u8 *ip, u32 type, u32 i)
{
    if (type == 0x1401)
        return ip[i];
    if (type == 0x1405)
        return (u32)ip[i * 4] | ((u32)ip[i * 4 + 1] << 8) | ((u32)ip[i * 4 + 2] << 16) | ((u32)ip[i * 4 + 3] << 24);
    return (u32)ip[i * 2] | ((u32)ip[i * 2 + 1] << 8);
}

static void r_tri(const r_surf_t *t, const r_vtx_t *a, const r_vtx_t *b, const r_vtx_t *c, const r_surf_t *tex,
                  const r_surf_t *tex1, u32 fmode)
{
    u32 t0 = r_now();
    r_tri_impl(t, a, b, c, tex, tex1, fmode);
    r_prof[P_TRI] += r_now() - t0;
    r_prof_tris++;
}

static int r_rect(const r_surf_t *t, const r_vtx_t *tri, const r_surf_t *tex, const r_surf_t *tex1, u32 fmode)
{
    u32 t0 = r_now();
    int ok = r_rect_impl(t, tri, tex, tex1, fmode);
    r_prof[ok ? P_RECT : P_RECTNO] += r_now() - t0;
    if (ok) {
        float w = tri[0].x - tri[1].x, h = tri[0].y - tri[1].y;
        for (int i = 2; i < 6; i++) {
            float dx = tri[0].x - tri[i].x, dy = tri[0].y - tri[i].y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx > (w < 0 ? -w : w)) w = dx;
            if (dy > (h < 0 ? -h : h)) h = dy;
        }
        if (w < 0) w = -w;
        if (h < 0) h = -h;
        r_prof_rects++;
        r_prof_rectpx += (u32)(s32)(w * h);
    }
    return ok;
}

/* indices == 0 -> DrawArrays order starting at `first`. */
static void r_draw_impl(u32 mode, u32 first, u32 count, u32 type, const u8 *ip)
{
    r_surf_t *t = r_target();
    const r_prog_t *pg;
    const r_uni_t *mu, *tu, *tu1;
    float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}, tident[4] = {1, 1, 0, 0};
    const float *m = ident, *tm = tident, *tm1 = tident;
    const r_surf_t *tex = 0, *tex1 = 0;
    u32 fmode;
    u32 n = 0;
    r_vtx_t v[3];
    r_draws++;
    if (!t || r_cur_prog >= R_MAXOBJ)
        return;
    r_draw_hits++;
    pg = &r_prog[r_cur_prog];
    mu = r_uni_find(r_uni_hash("vertex_matrix"), 0);
    tu = r_uni_find(r_uni_hash("texmat0"), 0);
    tu1 = r_uni_find(r_uni_hash("texmat1"), 0);
    if (mu) m = mu->f;
    if (tu) tm = tu->f;
    if (tu1) tm1 = tu1->f;
    fmode = pg->frag_tex;
    {
        u32 id = r_bound_tex[0];
        if (id && id < R_MAXOBJ && r_tex[id].px)
            tex = &r_tex[id];
        id = r_bound_tex[1];
        if (fmode == R_FM_MASK && id && id < R_MAXOBJ && r_tex[id].px)
            tex1 = &r_tex[id];
    }
    /* Draw log: one line per new kind of draw (program, mode, texture sizes, target size, skip reason), logged before
     * any early return so skipped draws show up too. skip: 0 drawn, 1 needs texture 0 but none bound, 2 bad mode. */
    {
        static u32 keys[96], nkeys;
        u32 skip = ((fmode == R_FM_MOD || fmode == R_FM_TEX || fmode == R_FM_MASK) && !tex) ? 1
                 : (mode != 4 && mode != 5 && mode != 6) ? 2 : 0;
        u32 key = r_cur_prog * 2654435761u ^ (tex ? (u32)tex->w * 40503u + (u32)tex->h : 0) ^ fmode << 28 ^ skip << 26 ^
                  (u32)t->w * 97u ^ (tex1 ? (u32)tex1->w << 12 : 0) ^ mode << 20;
        u32 seen = 0;
        for (u32 i = 0; i < nkeys; i++)
            if (keys[i] == key)
                seen = 1;
        if (!seen && nkeys < 96) {
            extern void j1_logbuf(const char *tag, const char *s, int n);
            r_vtx_t a, b;
            char line[150];
            int n = 0;
            u32 vals[12];
            keys[nkeys++] = key;
            r_setup_vertex(ip ? r_index(ip, type, 0) : first, pg, m, tm, tm1, &a);
            r_setup_vertex(ip ? r_index(ip, type, count > 2 ? 2 : 0) : first + (count > 2 ? 2 : 0), pg, m, tm, tm1, &b);
            vals[0] = r_cur_prog << 16 | fmode << 8 | skip << 4 | mode;
            vals[1] = tex ? (u32)tex->w << 16 | (u32)tex->h : 0;
            vals[2] = tex1 ? (u32)tex1->w << 16 | (u32)tex1->h : 0;
            vals[3] = (u32)t->w << 16 | (u32)t->h;
            vals[4] = (u32)(s32)a.x; vals[5] = (u32)(s32)a.y; vals[6] = (u32)(s32)b.x; vals[7] = (u32)(s32)b.y;
            vals[8] = (u32)(s32)(a.t[0] * 1000); vals[9] = (u32)(s32)(a.t[1] * 1000);
            vals[10] = (u32)(a.c[0] * 255) << 24 | (u32)(a.c[1] * 255) << 16 | (u32)(a.c[2] * 255) << 8 | (u32)(a.c[3] * 255);
            vals[11] = count;
            for (int i = 0; i < 12; i++) {
                for (int sh = 28; sh >= 0; sh -= 4)
                    line[n++] = "0123456789abcdef"[(vals[i] >> sh) & 15];
                line[n++] = ' ';
            }
            j1_logbuf("DRAWK", line, n);
        }
        if (skip)
            return;
    }
    if (mode == 5 && count == 4) { /* a 4-vertex strip: triangles 0,1,2 and 1,2,3 */
        r_vtx_t q[4], six[6];
        for (u32 i = 0; i < 4; i++)
            r_setup_vertex(ip ? r_index(ip, type, i) : first + i, pg, m, tm, tm1, &q[i]);
        six[0] = q[0]; six[1] = q[1]; six[2] = q[2]; six[3] = q[1]; six[4] = q[2]; six[5] = q[3];
        if (r_rect(t, six, tex, tex1, fmode))
            return;
        r_tri(t, &q[0], &q[1], &q[2], tex, tex1, fmode);
        r_tri(t, &q[1], &q[2], &q[3], tex, tex1, fmode);
        return;
    }
    if (mode == 4) { /* triangle list: offer each pair of triangles to the rectangle path */
        r_vtx_t six[6];
        u32 k = 0;
        for (u32 i = 0; i < count; i++) {
            r_setup_vertex(ip ? r_index(ip, type, i) : first + i, pg, m, tm, tm1, &six[k++]);
            if (k == 6) {
                if (!r_rect(t, six, tex, tex1, fmode)) {
                    r_tri(t, &six[0], &six[1], &six[2], tex, tex1, fmode);
                    r_tri(t, &six[3], &six[4], &six[5], tex, tex1, fmode);
                }
                k = 0;
            }
        }
        if (k >= 3)
            r_tri(t, &six[0], &six[1], &six[2], tex, tex1, fmode);
        return;
    }
    for (u32 i = 0; i < count; i++) {
        u32 idx = ip ? r_index(ip, type, i) : first + i;
        r_vtx_t cur;
        r_setup_vertex(idx, pg, m, tm, tm1, &cur);
        if (mode == 4) {
            v[n++] = cur;
            if (n == 3) { r_tri(t, &v[0], &v[1], &v[2], tex, tex1, fmode); n = 0; }
        } else if (mode == 5) {
            if (i < 2) v[i] = cur;
            else { v[2] = cur; r_tri(t, &v[0], &v[1], &v[2], tex, tex1, fmode); v[0] = v[1]; v[1] = v[2]; }
        } else {
            if (i == 0) v[0] = cur;
            else if (i == 1) v[1] = cur;
            else { v[2] = cur; r_tri(t, &v[0], &v[1], &v[2], tex, tex1, fmode); v[1] = cur; }
        }
    }
}

static void r_draw(u32 mode, u32 first, u32 count, u32 type, const u8 *ip)
{
    u32 t0 = r_now();
    r_draw_impl(mode, first, count, type, ip);
    r_prof[P_DRAW] += r_now() - t0;
}
