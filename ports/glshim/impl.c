/* Software GLES2 state machine, reached through the fake macro-context table. Functions named j1i_<glName> replace the
 * tracing stub of that entry; the first argument is always the GL context pointer, GL arguments follow.
 * The rasterizer lives in render.h. */
#include "render.h"

extern void j1_logstr(const char *tag, const char *s);
extern void j1_logbuf(const char *tag, const char *s, int n);

#define GL_NO_ERROR 0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_FRAGMENT_SHADER 0x8B30

static u32 next_id = 1;
static u32 r_shader_type[R_MAXOBJ];

static u32 alloc_names(s32 n, u32 *out)
{
    u32 first = next_id;
    for (s32 i = 0; i < n && out; i++)
        out[i] = next_id++;
    return first;
}

/* Called from the EAGL hook once a CoreSurface is attached to a texture: render straight into the surface memory. */
void j1_attach_surface(u32 tex, u32 *px, s32 w, s32 h, s32 stride_px)
{
    if (tex == 0 || tex >= R_MAXOBJ)
        return;
    r_tex[tex].px = px;
    r_tex[tex].w = w;
    r_tex[tex].h = h;
    r_tex[tex].stride = stride_px;
    r_tex[tex].external = 1;
    r_default = r_tex[tex];
}

/* attachImage:toCoreSurface: names a texture target, not a texture: the surface backs the texture bound to the active unit. */
void j1_attach_bound(u32 *px, s32 w, s32 h, s32 stride_px)
{
    j1_attach_surface(r_bound_tex[r_active_unit], px, w, h, stride_px);
}

u32 j1_bound_texture(void) { return r_bound_tex[r_active_unit]; }

static void r_log_counts(const char *tag)
{
    static u32 logs;
    char b[80];
    int n = 0;
    u32 v[6];
    if (logs++ >= 60)
        return;
    v[0] = r_draws; v[1] = r_clears; v[2] = r_cur_fbo; v[3] = (u32)(r_target() != 0); v[4] = r_bound_tex[r_active_unit];
    v[5] = r_draw_hits;
    while (*tag && n < 20)
        b[n++] = *tag++;
    for (int i = 0; i < 6; i++) {
        b[n++] = ' ';
        for (int sh = 28; sh >= 0; sh -= 4)
            b[n++] = "0123456789abcdef"[(v[i] >> sh) & 15];
    }
    j1_logbuf("RENDER", b, n);
}

/* ---- object names ---- */
void j1i_glGenTextures(void *c, s32 n, u32 *p) { alloc_names(n, p); }
void j1i_glGenBuffers(void *c, s32 n, u32 *p) { alloc_names(n, p); }
void j1i_glGenFramebuffers(void *c, s32 n, u32 *p) { alloc_names(n, p); }
void j1i_glGenRenderbuffers(void *c, s32 n, u32 *p) { alloc_names(n, p); }
void j1i_glDeleteTextures(void *c, s32 n, const u32 *p) {}
void j1i_glDeleteBuffers(void *c, s32 n, const u32 *p) {}
void j1i_glDeleteFramebuffers(void *c, s32 n, const u32 *p) {}
void j1i_glDeleteRenderbuffers(void *c, s32 n, const u32 *p) {}
void j1i_glBindFramebuffer(void *c, u32 target, u32 fb) { r_cur_fbo = fb; }
void j1i_glBindRenderbuffer(void *c, u32 target, u32 rb) { r_cur_rb = rb; }

u32 j1i_glCreateShader(void *c, u32 type)
{
    u32 id = next_id++;
    if (id < R_MAXOBJ)
        r_shader_type[id] = type;
    return id;
}

u32 j1i_glCreateProgram(void *c)
{
    u32 id = next_id++;
    if (id < R_MAXOBJ) {
        r_prog[id].pos = r_prog[id].col = r_prog[id].tc0 = r_prog[id].col1 = r_prog[id].tc1 = -1;
        r_prog[id].frag_tex = 0;
    }
    return id;
}

void j1i_glDeleteShader(void *c, u32 s) {}
void j1i_glDeleteProgram(void *c, u32 p) {}
void j1i_glUseProgram(void *c, u32 p) { r_cur_prog = p; }

void j1i_glAttachShader(void *c, u32 prog, u32 shader)
{
    if (prog < R_MAXOBJ && shader < R_MAXOBJ && r_shader_type[shader] == GL_FRAGMENT_SHADER)
        r_prog_frag[prog] = shader;
}

/* Log a tag and up to 10 values as hex words (diagnostics only). */
static void r_logvals(const char *tag, const u32 *v, int k)
{
    char line[100];
    int n = 0;
    while (*tag)
        line[n++] = *tag++;
    for (int i = 0; i < k && i < 10; i++) {
        line[n++] = ' ';
        for (int sh = 28; sh >= 0; sh -= 4)
            line[n++] = "0123456789abcdef"[(v[i] >> sh) & 15];
    }
    j1_logbuf("GL", line, n);
}

void j1i_glLinkProgram(void *c, u32 prog)
{
    if (prog < R_MAXOBJ) {
        const r_prog_t *p = &r_prog[prog];
        u32 v[7];
        r_prog[prog].frag_tex = r_shader_tex[r_prog_frag[prog]];
        v[0] = prog; v[1] = p->frag_tex; v[2] = r_prog_frag[prog];
        v[3] = (u32)p->pos; v[4] = (u32)p->col; v[5] = (u32)p->tc0; v[6] = (u32)p->col1 << 16 | ((u32)p->tc1 & 0xffff);
        r_logvals("LINK", v, 7);
    }
}

void j1i_glBindAttribLocation(void *c, u32 prog, u32 idx, const char *name)
{
    if (prog >= R_MAXOBJ || !name)
        return;
    if (r_has(name, 32, "vertex_position") && name[15] == 0)
        r_prog[prog].pos = (s32)idx;
    else if ((r_has(name, 32, "vertex_color0") && name[13] == 0) || (r_has(name, 32, "vertex_color") && name[12] == 0))
        r_prog[prog].col = (s32)idx;
    else if (r_has(name, 32, "vertex_color1") && name[13] == 0)
        r_prog[prog].col1 = (s32)idx;
    else if (r_has(name, 32, "vertex_texcoord0") && name[16] == 0)
        r_prog[prog].tc0 = (s32)idx;
    else if (r_has(name, 32, "vertex_texcoord1") && name[16] == 0)
        r_prog[prog].tc1 = (s32)idx;
}

void j1i_glShaderSource(void *c, u32 shader, s32 count, const char *const *str, const s32 *len)
{
    u32 f_tex = 0, f_mask = 0, f_c0s0 = 0, f_s0 = 0, f_c1a = 0, f_blur = 0, f_proj = 0;
    j1_logstr("SHADER", "source");
    if (shader < R_MAXOBJ)
        r_shader_tex[shader] = R_FM_COLOR;
    for (s32 i = 0; i < count && i < 8; i++) {
        const char *s = str[i];
        s32 n = 0;
        if (!s)
            continue;
        if (len && len[i] >= 0)
            n = len[i];
        else
            while (s[n])
                n++;
        f_tex |= (u32)r_has(s, n, "texture2D");
        f_mask |= (u32)r_has(s, n, "s0 * s1.a");
        f_c0s0 |= (u32)r_has(s, n, "color0 * s0");
        f_s0 |= (u32)r_has(s, n, "gl_FragData[0] = s0;");
        f_c1a |= (u32)r_has(s, n, "color0 * color1.a");
        f_blur |= (u32)r_has(s, n, "weight[");
        f_proj |= (u32)r_has(s, n, "texture2DProj");
        for (s32 o = 0; o < n && o < 1400; o += 150)
            j1_logbuf("SRC", s + o, n - o < 150 ? n - o : 150);
    }
    if (shader < R_MAXOBJ && r_shader_type[shader] == GL_FRAGMENT_SHADER)
        r_shader_tex[shader] = f_mask ? R_FM_MASK : (f_c0s0 || f_proj) ? R_FM_MOD : (f_s0 || f_blur) ? R_FM_TEX
                             : f_c1a ? R_FM_C1A : f_tex ? R_FM_MOD : R_FM_COLOR;
}

void j1i_glGetShaderiv(void *c, u32 shader, u32 pname, s32 *out)
{
    if (!out)
        return;
    *out = (pname == 0x8B81 /* COMPILE_STATUS */) ? 1 : 0;
}

void j1i_glGetProgramiv(void *c, u32 prog, u32 pname, s32 *out)
{
    if (!out)
        return;
    *out = (pname == 0x8B82 /* LINK_STATUS */ || pname == 0x8B83 /* VALIDATE_STATUS */) ? 1 : 0;
}

s32 j1i_glGetAttribLocation(void *c, u32 prog, const char *name)
{
    if (prog < R_MAXOBJ && name) {
        if (r_has(name, 32, "vertex_position") && name[15] == 0 && r_prog[prog].pos >= 0)
            return r_prog[prog].pos;
        if (r_has(name, 32, "vertex_color0") && name[13] == 0 && r_prog[prog].col >= 0)
            return r_prog[prog].col;
        if (r_has(name, 32, "vertex_texcoord0") && name[16] == 0 && r_prog[prog].tc0 >= 0)
            return r_prog[prog].tc0;
        if (r_has(name, 32, "vertex_color1") && name[13] == 0 && r_prog[prog].col1 >= 0)
            return r_prog[prog].col1;
        if (r_has(name, 32, "vertex_texcoord1") && name[16] == 0 && r_prog[prog].tc1 >= 0)
            return r_prog[prog].tc1;
    }
    return name ? (name[0] + name[1]) & 7 : 0;
}

s32 j1i_glGetUniformLocation(void *c, u32 prog, const char *name) { return (s32)r_uni_hash(name); }

/* ---- uniforms / constant attributes ---- */
void j1i_glUniform4f(void *c, s32 loc, float x, float y, float z, float w)
{
    r_uni_t *u = r_uni_find((u32)loc, 1);
    if (!u)
        return;
    u->f[0] = x; u->f[1] = y; u->f[2] = z; u->f[3] = w;
}

void j1i_glUniform4fv(void *c, s32 loc, s32 count, const float *v)
{
    r_uni_t *u = r_uni_find((u32)loc, 1);
    if (u && v)
        for (int i = 0; i < 4; i++)
            u->f[i] = v[i];
}

void j1i_glUniformMatrix4fv(void *c, s32 loc, s32 count, u32 transpose, const float *v)
{
    r_uni_t *u = r_uni_find((u32)loc, 1);
    if (!u || !v)
        return;
    for (int i = 0; i < 16; i++)
        u->f[i] = transpose ? v[(i & 3) * 4 + (i >> 2)] : v[i];
}

void j1i_glVertexAttrib4f(void *c, u32 idx, float x, float y, float z, float w)
{
    if (idx >= 16)
        return;
    r_attr[idx].cv[0] = x; r_attr[idx].cv[1] = y; r_attr[idx].cv[2] = z; r_attr[idx].cv[3] = w;
    r_attr[idx].cvset = 1;
}

void j1i_glVertexAttrib4fv(void *c, u32 idx, const float *v)
{
    if (idx < 16 && v) {
        for (int i = 0; i < 4; i++)
            r_attr[idx].cv[i] = v[i];
        r_attr[idx].cvset = 1;
    }
}

void j1i_glVertexAttribPointer(void *c, u32 idx, s32 size, u32 type, u32 norm, s32 stride, const void *ptr)
{
    if (idx >= 16)
        return;
    r_attr[idx].size = size;
    r_attr[idx].type = (s32)type;
    r_attr[idx].norm = (u8)norm;
    r_attr[idx].stride = stride;
    r_attr[idx].ptr = (const u8 *)ptr;
    r_attr[idx].buf = r_bound_array;
}

void j1i_glEnableVertexAttribArray(void *c, u32 idx) { if (idx < 16) r_attr[idx].enabled = 1; }
void j1i_glDisableVertexAttribArray(void *c, u32 idx) { if (idx < 16) r_attr[idx].enabled = 0; }

/* ---- buffers ---- */
void j1i_glBindBuffer(void *c, u32 target, u32 id)
{
    if (target == 0x8892)
        r_bound_array = id;
    else if (target == 0x8893)
        r_bound_element = id;
}

void j1i_glBufferData(void *c, u32 target, s32 size, const void *data, u32 usage)
{
    u32 id = target == 0x8893 ? r_bound_element : r_bound_array;
    r_buf_t *b;
    if (id == 0 || id >= R_MAXOBJ || size < 0)
        return;
    b = &r_bufs[id];
    if ((u32)size > b->cap) {
        b->p = (u8 *)r_alloc((u32)size);
        b->cap = b->p ? (u32)size : 0;
    }
    b->size = (u32)size;
    if (b->p && data)
        r_copy(b->p, data, (u32)size);
}

void j1i_glBufferSubData(void *c, u32 target, s32 off, s32 size, const void *data)
{
    u32 id = target == 0x8893 ? r_bound_element : r_bound_array;
    r_buf_t *b;
    if (id == 0 || id >= R_MAXOBJ || off < 0 || size < 0)
        return;
    b = &r_bufs[id];
    if (b->p && data && (u32)(off + size) <= b->cap)
        r_copy(b->p + off, data, (u32)size);
}

/* ---- textures / renderbuffers / framebuffers ---- */
void j1i_glActiveTexture(void *c, u32 unit) { r_active_unit = unit >= 0x84C0 && unit < 0x84C8 ? unit - 0x84C0 : 0; }

void j1i_glBindTexture(void *c, u32 target, u32 id)
{
    r_bound_tex[r_active_unit] = id;
}

static u32 r_conv_in(const u8 *p, u32 fmt)
{
    if (fmt == 0x1908)      /* RGBA */
        return ((u32)p[3] << 24) | ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    if (fmt == 0x80E1)      /* BGRA */
        return ((u32)p[3] << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | p[0];
    if (fmt == 0x1907)      /* RGB */
        return 0xFF000000u | ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    if (fmt == 0x1906)      /* ALPHA */
        return (u32)p[0] << 24;
    if (fmt == 0x1909)      /* LUMINANCE */
        return 0xFF000000u | ((u32)p[0] << 16) | ((u32)p[0] << 8) | p[0];
    if (fmt == 0x190A)      /* LUMINANCE_ALPHA */
        return ((u32)p[1] << 24) | ((u32)p[0] << 16) | ((u32)p[0] << 8) | p[0];
    return 0;
}

static u32 r_bpp(u32 fmt)
{
    return fmt == 0x1908 || fmt == 0x80E1 ? 4 : fmt == 0x1907 ? 3 : fmt == 0x190A ? 2 : 1;
}

/* Pixel unpacking (glPixelStorei): source rows are padded to GL_UNPACK_ALIGNMENT (default 4) and may be longer than
 * the upload (GL_UNPACK_ROW_LENGTH). Ignoring this sheared every texture whose row is not a multiple of 4 bytes
 * (8-bit glyph textures: the diagonal smear seen on the S5 lock screen, run 66). */
static s32 r_unpack_align = 4, r_unpack_rowlen = 0, r_unpack_rowbytes = 0;

void j1i_glPixelStorei(void *c, u32 pname, s32 param)
{
    static int logs;
    if (logs < 16) {
        char b[24] = "PSTORE ";
        for (int i = 0; i < 4; i++)
            b[7 + i] = "0123456789abcdef"[(pname >> (12 - 4 * i)) & 15];
        b[11] = ' ';
        for (int i = 0; i < 8; i++)
            b[12 + i] = "0123456789abcdef"[((u32)param >> (28 - 4 * i)) & 15];
        logs++;
        j1_logbuf("GL", b, 20);
    }
    if (pname == 0x0CF5 && (param == 1 || param == 2 || param == 4 || param == 8)) /* GL_UNPACK_ALIGNMENT */
        r_unpack_align = param;
    else if (pname == 0x0CF2 && param >= 0)                                         /* GL_UNPACK_ROW_LENGTH */
        r_unpack_rowlen = param;
    else if (pname == 0x8A16 && param >= 0) /* GL_UNPACK_ROW_BYTES_APPLE: CGImage rows padded to 16/64 bytes (S5 clock, run 68) */
        r_unpack_rowbytes = param;
}

static usize r_src_pitch(s32 w, u32 bpp)
{
    usize row;
    if (r_unpack_rowbytes > 0) /* APPLE_row_bytes overrides ROW_LENGTH and ALIGNMENT */
        return (usize)r_unpack_rowbytes;
    row = (usize)(r_unpack_rowlen > 0 ? r_unpack_rowlen : w) * bpp;
    return (row + (usize)r_unpack_align - 1) & ~((usize)r_unpack_align - 1);
}

/* Fill texture id from memory with an explicit source pitch (IOSurface contents that are not 32-bit BGRA). */
void j1_tex_from_mem(u32 id, const u8 *src, s32 w, s32 h, usize pitch, u32 fmt)
{
    r_surf_t *t;
    u32 bpp = r_bpp(fmt);
    if (id == 0 || id >= R_MAXOBJ || w <= 0 || h <= 0 || !src)
        return;
    u32 pt0 = r_now();
    t = &r_tex[id];
    r_prof_texpx += (u32)(w * h);
    if (t->external || !t->px || t->w * t->h < w * h) {
        t->px = (u32 *)r_alloc((u32)w * (u32)h * 4);
        t->external = 0;
        if (!t->px)
            return;
    }
    t->w = w; t->h = h; t->stride = w;
    for (s32 y = 0; y < h; y++)
        for (s32 x = 0; x < w; x++)
            t->px[y * w + x] = r_conv_in(src + (usize)y * pitch + (usize)x * bpp, fmt);
    r_prof[P_TEX] += r_now() - pt0;
}

/* Sample texture id straight from 32-bit BGRA surface memory (BGRA little-endian words are already our ARGB u32). Unlike
 * j1_attach_surface this does not make it the default render target. */
void j1_tex_external(u32 id, u32 *px, s32 w, s32 h, s32 stride_px)
{
    if (id == 0 || id >= R_MAXOBJ || !px)
        return;
    r_tex[id].px = px;
    r_tex[id].w = w;
    r_tex[id].h = h;
    r_tex[id].stride = stride_px;
    r_tex[id].external = 1;
}

void j1i_glTexImage2D(void *c, u32 target, s32 level, s32 ifmt, s32 w, s32 h, s32 border, u32 fmt, u32 type,
                      const void *pixels)
{
    u32 id = r_bound_tex[r_active_unit];
    r_surf_t *t;
    static int logs;
    if (logs < 60) {
        u32 v[8] = {id, (u32)level, (u32)ifmt, (u32)w << 16 | (u32)h, fmt, type, (u32)(pixels != 0), (u32)r_src_pitch(w, r_bpp(fmt))};
        logs++;
        r_logvals("TEXIMG", v, 8);
    }
    if (level != 0 || id == 0 || id >= R_MAXOBJ || w <= 0 || h <= 0)
        return;
    u32 pt0 = r_now();
    t = &r_tex[id];
    r_prof_texpx += (u32)(w * h);
    if (t->external || !t->px || t->w * t->h < w * h) {
        /* a texture name that previously sampled an IOSurface becomes an ordinary texture again */
        t->px = (u32 *)r_alloc((u32)w * (u32)h * 4);
        t->external = 0;
        if (!t->px)
            return;
    }
    t->w = w; t->h = h; t->stride = w;
    if (pixels && type == 0x1401) {
        const u8 *src = (const u8 *)pixels;
        u32 bpp = r_bpp(fmt);
        usize pitch = r_src_pitch(w, bpp);
        /* CA hands CGImage bitmaps (rows padded to 32 bytes) to glTexImage2D as BGRA without setting any row pitch
         * (S5 runs 68-74: never a glPixelStorei call). Measured rows: 173 px -> 704 B, 197 px -> 800 B (its buffer
         * ends exactly at 129 x 800 rounded to pages: 64-byte padding read past it and crashed backboardd in run
         * 74), 460 px -> 1856 B ("slide to unlock", 4 px/row shear in run 72); 208/232 px (date) are already
         * multiples of 32. */
        if (fmt == 0x80E1 && r_unpack_rowbytes == 0 && r_unpack_rowlen == 0)
            pitch = ((usize)w * 4 + 31) & ~(usize)31;
        for (s32 y = 0; y < h; y++)
            for (s32 x = 0; x < w; x++)
                t->px[y * w + x] = r_conv_in(src + (usize)y * pitch + (usize)x * bpp, fmt);
    } else {
        for (s32 i = 0; i < w * h; i++)
            t->px[i] = 0;
    }
    r_prof[P_TEX] += r_now() - pt0;
}

void j1i_glTexSubImage2D(void *c, u32 target, s32 level, s32 xo, s32 yo, s32 w, s32 h, u32 fmt, u32 type,
                         const void *pixels)
{
    u32 id = r_bound_tex[r_active_unit];
    r_surf_t *t;
    if (level != 0 || id == 0 || id >= R_MAXOBJ || !pixels || type != 0x1401)
        return;
    t = &r_tex[id];
    if (!t->px)
        return;
    {
        const u8 *src = (const u8 *)pixels;
        u32 bpp = r_bpp(fmt);
        usize pitch = r_src_pitch(w, bpp);
        u32 pt0 = r_now();
        for (s32 y = 0; y < h; y++)
            for (s32 x = 0; x < w; x++)
                if (xo + x >= 0 && xo + x < t->w && yo + y >= 0 && yo + y < t->h)
                    t->px[(yo + y) * t->stride + xo + x] = r_conv_in(src + (usize)y * pitch + (usize)x * bpp, fmt);
        r_prof[P_TEX] += r_now() - pt0;
        r_prof_texpx += (u32)(w * h);
    }
}

void j1i_glRenderbufferStorage(void *c, u32 target, u32 fmt, s32 w, s32 h)
{
    r_surf_t *r;
    if (r_cur_rb == 0 || r_cur_rb >= R_MAXOBJ || w <= 0 || h <= 0)
        return;
    r = &r_rb[r_cur_rb];
    if (!r->external && (!r->px || r->w * r->h < w * h))
        r->px = (u32 *)r_alloc((u32)w * (u32)h * 4);
    r->w = w; r->h = h; r->stride = w;
}

void j1i_glFramebufferTexture2D(void *c, u32 target, u32 attachment, u32 textarget, u32 tex, s32 level)
{
    if (r_cur_fbo == 0 || r_cur_fbo >= R_MAXOBJ || attachment != 0x8CE0)
        return;
    r_fbo[r_cur_fbo].color_tex = tex;
    r_fbo[r_cur_fbo].color_rb = 0;
}

void j1i_glFramebufferRenderbuffer(void *c, u32 target, u32 attachment, u32 rbtarget, u32 rb)
{
    if (r_cur_fbo == 0 || r_cur_fbo >= R_MAXOBJ || attachment != 0x8CE0)
        return;
    r_fbo[r_cur_fbo].color_rb = rb;
    r_fbo[r_cur_fbo].color_tex = 0;
}

/* ---- fixed state ---- */
void j1i_glClearColor(void *c, float r, float g, float b, float a)
{
    r_clear[0] = r; r_clear[1] = g; r_clear[2] = b; r_clear[3] = a;
}

void j1i_glClear(void *c, u32 mask)
{
    if (mask & 0x4000u)
        r_do_clear();
}

void j1i_glScissor(void *c, s32 x, s32 y, s32 w, s32 h)
{
    r_scissor[0] = x; r_scissor[1] = y; r_scissor[2] = w; r_scissor[3] = h;
}

void j1i_glEnable(void *c, u32 cap)
{
    if (cap == 0x0C11) r_scissor_on = 1;
    else if (cap == 0x0BE2) r_blend_on = 1;
}

void j1i_glDisable(void *c, u32 cap)
{
    if (cap == 0x0C11) r_scissor_on = 0;
    else if (cap == 0x0BE2) r_blend_on = 0;
}

void j1i_glBlendFunc(void *c, u32 s, u32 d) { r_sfac = s; r_dfac = d; }
void j1i_glBlendFuncSeparate(void *c, u32 s, u32 d, u32 sa, u32 da) { r_sfac = s; r_dfac = d; }

void j1i_glDrawArrays(void *c, u32 mode, s32 first, s32 count)
{
    if (first >= 0 && count > 0)
        r_draw(mode, (u32)first, (u32)count, 0, 0);
}

void j1i_glDrawElements(void *c, u32 mode, s32 count, u32 type, const void *indices)
{
    const u8 *ip = (const u8 *)indices;
    if (count <= 0)
        return;
    if (r_bound_element) {
        if (r_bound_element >= R_MAXOBJ || !r_bufs[r_bound_element].p)
            return;
        ip = r_bufs[r_bound_element].p + (usize)indices;
    }
    if (ip)
        r_draw(mode, 0, (u32)count, type, ip);
}

/* ---- queries ---- */
u32 j1i_glGetError(void *c) { return GL_NO_ERROR; }
u32 j1i_glCheckFramebufferStatus(void *c, u32 target) { return GL_FRAMEBUFFER_COMPLETE; }

const char *j1i_glGetString(void *c, u32 name)
{
    switch (name) {
    case 0x1F00: return "Apple Inc.";
    case 0x1F01: return "J1 Software Renderer";
    case 0x1F02: return "OpenGL ES 2.0 J1-soft";
    case 0x1F03:
        return "GL_OES_depth_texture GL_OES_packed_depth_stencil GL_OES_rgb8_rgba8 GL_OES_texture_npot GL_EXT_texture_format_BGRA8888 "
               "GL_APPLE_texture_max_level GL_OES_vertex_array_object";
    case 0x8B8C: return "OpenGL ES GLSL ES 1.00";
    }
    return "";
}

void j1i_glGetIntegerv(void *c, u32 pname, s32 *out)
{
    if (!out)
        return;
    switch (pname) {
    case 0x0D33: *out = 2048; break;       /* MAX_TEXTURE_SIZE */
    case 0x0D3A: out[0] = out[1] = 2048; break; /* MAX_VIEWPORT_DIMS */
    case 0x0BA2: for (int i = 0; i < 4; i++) out[i] = r_viewport[i]; break; /* VIEWPORT */
    case 0x8869: *out = 16; break;         /* MAX_VERTEX_ATTRIBS */
    case 0x8872: *out = 8; break;          /* MAX_TEXTURE_IMAGE_UNITS */
    case 0x8B4C: *out = 8; break;          /* MAX_VERTEX_TEXTURE_IMAGE_UNITS */
    case 0x8B4D: *out = 16; break;         /* MAX_COMBINED_TEXTURE_IMAGE_UNITS */
    case 0x84E8: *out = 2048; break;       /* MAX_RENDERBUFFER_SIZE */
    case 0x8DFB: *out = 128; break;        /* MAX_VERTEX_UNIFORM_VECTORS */
    case 0x8DFC: *out = 8; break;          /* MAX_VARYING_VECTORS */
    case 0x8DFD: *out = 64; break;         /* MAX_FRAGMENT_UNIFORM_VECTORS */
    case 0x8CA6: *out = (s32)r_cur_fbo; break;  /* FRAMEBUFFER_BINDING */
    case 0x8CA7: *out = (s32)r_cur_rb; break;   /* RENDERBUFFER_BINDING */
    case 0x8B8D: *out = (s32)r_cur_prog; break; /* CURRENT_PROGRAM */
    default: *out = 0; break;
    }
}

void j1i_glViewport(void *c, s32 x, s32 y, s32 w, s32 h)
{
    r_viewport[0] = x;
    r_viewport[1] = y;
    r_viewport[2] = w;
    r_viewport[3] = h;
}

void j1i_glFlush(void *c) { r_log_counts("flush draws,clears,fbo,target,tex:"); }
void j1i_glFinish(void *c) { r_log_counts("finish draws,clears,fbo,target,tex:"); }

/* ---- rarely used calls: just enough that callers reading outputs get defined values ---- */
void j1i_glGetShaderInfoLog(void *c, u32 shader, s32 bufsize, s32 *len, char *log)
{
    if (len) *len = 0;
    if (log && bufsize > 0) log[0] = 0;
}

void j1i_glGetProgramInfoLog(void *c, u32 prog, s32 bufsize, s32 *len, char *log)
{
    if (len) *len = 0;
    if (log && bufsize > 0) log[0] = 0;
}

void j1i_glGetShaderSource(void *c, u32 shader, s32 bufsize, s32 *len, char *src)
{
    if (len) *len = 0;
    if (src && bufsize > 0) src[0] = 0;
}

void j1i_glValidateProgram(void *c, u32 prog) {}

u32 j1i_glFenceSync(void *c, u32 condition, u32 flags) { return 1; }
u32 j1i_glClientWaitSync(void *c, u32 sync, u32 flags, u32 timeout_lo, u32 timeout_hi) { return 0x911A; /* ALREADY_SIGNALED */ }
void j1i_glWaitSync(void *c, u32 sync, u32 flags, u32 timeout_lo, u32 timeout_hi) {}
void j1i_glDeleteSync(void *c, u32 sync) {}
u32 j1i_glIsSync(void *c, u32 sync) { return sync != 0; }

void j1i_glGetFloatv(void *c, u32 pname, float *out)
{
    s32 iv[4] = {0, 0, 0, 0};
    if (!out)
        return;
    j1i_glGetIntegerv(c, pname, iv);
    if (pname == 0x0BA2 || pname == 0x0C22) {       /* VIEWPORT, COLOR_CLEAR_VALUE */
        for (int i = 0; i < 4; i++)
            out[i] = pname == 0x0BA2 ? (float)iv[i] : r_clear[i];
        return;
    }
    out[0] = (float)iv[0];
}

void j1i_glGetBooleanv(void *c, u32 pname, u8 *out)
{
    if (out) out[0] = out[1] = out[2] = out[3] = 0;
}

void j1i_glGetTexParameteriv(void *c, u32 target, u32 pname, s32 *out)
{
    if (out) *out = pname == 0x2801 || pname == 0x2800 ? 0x2601 /* LINEAR */ : (pname == 0x2802 || pname == 0x2803 ? 0x812F : 0);
}

void j1i_glGetFramebufferAttachmentParameteriv(void *c, u32 target, u32 attach, u32 pname, s32 *out)
{
    if (out) *out = 0;
}

void j1i_glGetRenderbufferParameteriv(void *c, u32 target, u32 pname, s32 *out)
{
    if (!out)
        return;
    *out = 0;
    if (r_cur_rb && r_cur_rb < R_MAXOBJ) {
        if (pname == 0x8D42) *out = r_rb[r_cur_rb].w;        /* RENDERBUFFER_WIDTH */
        else if (pname == 0x8D43) *out = r_rb[r_cur_rb].h;   /* RENDERBUFFER_HEIGHT */
    }
}

void j1i_glGetShaderPrecisionFormat(void *c, u32 shadertype, u32 precisiontype, s32 *range, s32 *precision)
{
    int is_int = precisiontype >= 0x8DF3;     /* LOW_INT, MEDIUM_INT, HIGH_INT */
    if (range) { range[0] = is_int ? 31 : 127; range[1] = is_int ? 30 : 127; }
    if (precision) *precision = is_int ? 0 : 23;
}

void j1i_glGenVertexArraysOES(void *c, s32 n, u32 *p) { alloc_names(n, p); }
void j1i_glGenVertexArrays(void *c, s32 n, u32 *p) { alloc_names(n, p); }
void j1i_glBindVertexArrayOES(void *c, u32 id) {}
void j1i_glBindVertexArray(void *c, u32 id) {}
void j1i_glDeleteVertexArraysOES(void *c, s32 n, const u32 *p) {}
void j1i_glDeleteVertexArrays(void *c, s32 n, const u32 *p) {}

void j1i_glGenQueriesEXT(void *c, s32 n, u32 *p) { alloc_names(n, p); }
void j1i_glGetQueryObjectuivEXT(void *c, u32 id, u32 pname, u32 *out)
{
    if (out) *out = pname == 0x8867 ? 1 : 0;      /* QUERY_RESULT_AVAILABLE */
}

u32 j1i_glIsTexture(void *c, u32 id) { return id != 0; }
u32 j1i_glIsBuffer(void *c, u32 id) { return id != 0; }
u32 j1i_glIsFramebuffer(void *c, u32 id) { return id != 0; }
u32 j1i_glIsRenderbuffer(void *c, u32 id) { return id != 0; }
u32 j1i_glIsProgram(void *c, u32 id) { return id != 0; }
u32 j1i_glIsShader(void *c, u32 id) { return id != 0; }
u32 j1i_glIsEnabled(void *c, u32 cap) { return cap == 0x0C11 ? r_scissor_on : cap == 0x0BE2 ? r_blend_on : 0; }

void j1i_glDiscardFramebufferEXT(void *c, u32 target, s32 n, const u32 *attachments) {}

/* Scalar uniforms the CA shaders do not use are still remembered so a later shader variant can read them. */
void j1i_glUniform1i(void *c, s32 loc, s32 x) {}
void j1i_glUniform1f(void *c, s32 loc, float x)
{
    r_uni_t *u = r_uni_find((u32)loc, 1);
    if (u) u->f[0] = x;
}

void j1i_glUniform2f(void *c, s32 loc, float x, float y)
{
    r_uni_t *u = r_uni_find((u32)loc, 1);
    if (u) { u->f[0] = x; u->f[1] = y; }
}

void j1i_glUniform3f(void *c, s32 loc, float x, float y, float z)
{
    r_uni_t *u = r_uni_find((u32)loc, 1);
    if (u) { u->f[0] = x; u->f[1] = y; u->f[2] = z; }
}

void j1i_glReadPixels(void *c, s32 x, s32 y, s32 w, s32 h, u32 fmt, u32 type, void *pixels)
{
    r_surf_t *t = r_target();
    u8 *out = (u8 *)pixels;
    u32 pt0 = r_now();
    if (!t || !out || type != 0x1401)
        return;
    for (s32 j = 0; j < h; j++)
        for (s32 i = 0; i < w; i++) {
            u32 p = (x + i >= 0 && x + i < t->w && y + j >= 0 && y + j < t->h) ? t->px[(y + j) * t->stride + x + i] : 0;
            u8 r = (u8)(p >> 16), g = (u8)(p >> 8), b = (u8)p, a = (u8)(p >> 24);
            *out++ = fmt == 0x80E1 ? b : r;
            *out++ = g;
            *out++ = fmt == 0x80E1 ? r : b;
            *out++ = a;
        }
    r_prof[P_READ] += r_now() - pt0;
}

/* Called on every attachImage (one per CA frame): cumulative draw/clear counts, first 40 frames then every 64th. */
/* Per-frame profiler tick (run 109): called from -[EAGLContext swapNotification:forTransaction:onLayer:], which CA
 * sends every frame (attachImage only comes on (re)attach). */
void j1_prof_tick(void)
{
    static u32 frames;
    static u32 win_start, win_frames;
    frames++;
    {
        u32 now = r_now();
        if (!win_start)
            win_start = now;
        if (++win_frames == 32) {
            /* PROF window: total ticks of 32 frames, then ticks per phase, then counts. 24 ticks = 1 us. */
            char b[200];
            int n = 0;
            u32 v[16];
            v[0] = now - win_start; v[1] = r_prof[P_DRAW]; v[2] = r_prof[P_RECT]; v[3] = r_prof[P_RECTNO];
            v[4] = r_prof[P_TRI]; v[5] = r_prof[P_CLEAR]; v[6] = r_prof[P_TEX]; v[7] = r_prof[P_READ];
            v[8] = r_draws; v[9] = r_prof_rects; v[10] = r_prof_tris; v[11] = r_prof_rectpx; v[12] = r_prof_clearpx;
            v[13] = r_prof_texpx; v[14] = frames;
            for (int i = 0; i < 15; i++) {
                for (int sh = 28; sh >= 0; sh -= 4)
                    b[n++] = "0123456789abcdef"[(v[i] >> sh) & 15];
                b[n++] = ' ';
            }
            j1_logbuf("PROF", b, n);
            {
                char rb[160];
                int rn = 0;
                for (int i = 1; i < 16; i++) {
                    for (int sh = 28; sh >= 0; sh -= 4)
                        rb[rn++] = "0123456789abcdef"[(r_rej[i] >> sh) & 15];
                    rb[rn++] = ' ';
                    r_rej[i] = 0;
                }
                j1_logbuf("RREJ", rb, rn);
            }
            for (int i = 0; i < P_N; i++)
                r_prof[i] = 0;
            r_prof_rects = r_prof_tris = r_prof_rectpx = r_prof_clearpx = r_prof_texpx = 0;
            r_draws = 0;
            win_frames = 0;
            win_start = now;
        }
    }
}

void j1_log_frame(void)
{
    static u32 frames;
    frames++;
    if (frames <= 40 || (frames & 63) == 0) {
        char b[120];
        int n = 0;
        const char *tag = "frame draws,clears,fbo,target,tex,hits,n:";
        u32 v[7];
        v[0] = r_draws; v[1] = r_clears; v[2] = r_cur_fbo; v[3] = (u32)(r_target() != 0); v[4] = r_bound_tex[r_active_unit];
        v[5] = r_draw_hits; v[6] = frames;
        while (*tag && n < 44)
            b[n++] = *tag++;
        for (int i = 0; i < 7; i++) {
            b[n++] = ' ';
            for (int sh = 28; sh >= 0; sh -= 4)
                b[n++] = "0123456789abcdef"[(v[i] >> sh) & 15];
        }
        j1_logbuf("RENDER", b, n);
    }
}
