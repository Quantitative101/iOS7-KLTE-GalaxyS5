/* Native benchmark + equivalence check for the software renderer's fast paths (r_fast) against the reference paths.
 * Draws a home-screen-like frame the way CoreAnimation does (pixel-space vertices through an orthographic
 * vertex_matrix, indexed triangle lists of quads, premultiplied blending), plus non-rectangular draws that must keep
 * using the general path. Prints time per frame for both modes and how many pixels differ.
 * Build and run: ports/glshim/test/bench.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../impl.c"

void j1_logstr(const char *tag, const char *s) { (void)tag; (void)s; }
void j1_logbuf(const char *tag, const char *s, int n) { (void)tag; (void)s; (void)n; }

#define SW 480
#define SH 800
static u32 screen[SW * SH], ref[SW * SH];
static u32 wall[SW * SH], icon[60 * 60], glyphs[256 * 16];
static float ortho[16];
static u32 prog_tex, prog_col, prog_mask, t_screen, t_wall, t_icon, t_glyph;

static u32 make_prog(const char *body)
{
    u32 fs = j1i_glCreateShader(0, 0x8B30), prog = j1i_glCreateProgram(0);
    s32 len = (s32)strlen(body);
    j1i_glShaderSource(0, fs, 1, &body, &len);
    j1i_glAttachShader(0, prog, fs);
    j1i_glBindAttribLocation(0, prog, 0, "vertex_position");
    j1i_glBindAttribLocation(0, prog, 1, "vertex_color0");
    j1i_glBindAttribLocation(0, prog, 2, "vertex_texcoord0");
    j1i_glBindAttribLocation(0, prog, 3, "vertex_texcoord1");
    j1i_glLinkProgram(0, prog);
    j1i_glUseProgram(0, prog);
    j1i_glUniformMatrix4fv(0, j1i_glGetUniformLocation(0, prog, "vertex_matrix"), 1, 0, ortho);
    j1i_glUniform4f(0, j1i_glGetUniformLocation(0, prog, "texmat0"), 1, 1, 0, 0);
    j1i_glUniform4f(0, j1i_glGetUniformLocation(0, prog, "texmat1"), 1, 1, 0, 0);
    return prog;
}

static u32 make_tex(u32 *px, int w, int h)
{
    u32 id;
    j1i_glGenTextures(0, 1, &id);
    j1i_glBindTexture(0, 0x0DE1, id);
    j1_tex_external(id, px, w, h, w);
    return id;
}

/* One quad as an indexed triangle list (CA style): corners in pixels, texcoords, flat colour. */
static void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const float *rgba)
{
    float pos[8] = {x0, y0, x1, y0, x0, y1, x1, y1};
    float tc[8] = {u0, v0, u1, v0, u0, v1, u1, v1};
    float col[16];
    static const u16 idx[6] = {0, 1, 2, 2, 1, 3};
    for (int i = 0; i < 4; i++)
        memcpy(col + 4 * i, rgba, 16);
    j1i_glVertexAttribPointer(0, 0, 2, 0x1406, 0, 0, pos);
    j1i_glVertexAttribPointer(0, 1, 4, 0x1406, 0, 0, col);
    j1i_glVertexAttribPointer(0, 2, 2, 0x1406, 0, 0, tc);
    j1i_glVertexAttribPointer(0, 3, 2, 0x1406, 0, 0, tc);
    j1i_glDrawElements(0, 4, 6, 0x1403, idx);
}

/* A rotated quad (general path) with a per-vertex colour gradient. */
static void rotated(float cx, float cy, float r, float ang)
{
    float pos[8], tc[8] = {0, 0, 1, 0, 0, 1, 1, 1}, col[16];
    static const u16 idx[6] = {0, 1, 2, 2, 1, 3};
    for (int i = 0; i < 4; i++) {
        float a = ang + (i == 0 ? 3.927f : i == 1 ? 5.498f : i == 2 ? 2.356f : 0.785f);
        pos[2 * i] = cx + r * __builtin_cosf(a);
        pos[2 * i + 1] = cy + r * __builtin_sinf(a);
        col[4 * i] = 0.25f * i; col[4 * i + 1] = 0.5f; col[4 * i + 2] = 1 - 0.25f * i; col[4 * i + 3] = 1;
    }
    j1i_glVertexAttribPointer(0, 0, 2, 0x1406, 0, 0, pos);
    j1i_glVertexAttribPointer(0, 1, 4, 0x1406, 0, 0, col);
    j1i_glVertexAttribPointer(0, 2, 2, 0x1406, 0, 0, tc);
    j1i_glDrawElements(0, 4, 6, 0x1403, idx);
}

static void frame(void)
{
    static const float white[4] = {1, 1, 1, 1}, dock[4] = {0.35f, 0.35f, 0.38f, 0.6f}, half[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    j1i_glDisable(0, 0x0BE2);
    j1i_glUseProgram(0, prog_tex);
    j1i_glBindTexture(0, 0x0DE1, t_wall);
    quad(0, 0, SW, SH, 0, 0, 1, 1, white);                      /* wallpaper, opaque */
    j1i_glEnable(0, 0x0BE2);
    j1i_glBlendFunc(0, 1, 0x303);                               /* premultiplied */
    j1i_glUseProgram(0, prog_col);
    quad(0, 680, SW, SH, 0, 0, 1, 1, dock);                      /* dock */
    j1i_glUseProgram(0, prog_tex);
    j1i_glBindTexture(0, 0x0DE1, t_icon);
    for (int r = 0; r < 5; r++)                                 /* 20 icons + 4 dock icons */
        for (int c = 0; c < 4; c++)
            quad(30 + c * 115, 40 + r * 125, 90 + c * 115, 100 + r * 125, 0, 0, 1, 1, white);
    for (int c = 0; c < 4; c++)
        quad(30 + c * 115, 710, 90 + c * 115, 770, 0, 0, 1, 1, white);
    j1i_glBindTexture(0, 0x0DE1, t_glyph);
    for (int i = 0; i < 160; i++)                               /* labels and status bar: glyph quads, 1:1 like CA */
        quad(20 + (i % 40) * 11, 105 + (i / 40) * 125, 30 + (i % 40) * 11, 119 + (i / 40) * 125,
             (i % 16) * 16 / 256.0f, 0, ((i % 16) * 16 + 10) / 256.0f, 14 / 16.0f, half);
    j1i_glUseProgram(0, prog_mask);                             /* "slide to unlock" style mask */
    j1i_glActiveTexture(0, 0x84C1);
    j1i_glBindTexture(0, 0x0DE1, t_icon);
    j1i_glActiveTexture(0, 0x84C0);
    j1i_glBindTexture(0, 0x0DE1, t_glyph);
    quad(40, 600, 440, 640, 0, 0, 1, 1, white);
    j1i_glUseProgram(0, prog_col);
    rotated(240, 400, 80, 0.3f);                                /* general path */
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(void)
{
    u32 fbo;
    int frames = 40, diff = 0, maxd = 0, big = 0;
    double t0, tref, tfast;
    memset(ortho, 0, sizeof ortho);
    ortho[0] = 2.0f / SW; ortho[5] = 2.0f / SH; ortho[10] = 1; ortho[12] = -1; ortho[13] = -1; ortho[15] = 1;
    for (int i = 0; i < SW * SH; i++)
        wall[i] = 0xff000000u | (u32)((i * 2654435761u) >> 8 & 0x3f3f3f);
    for (int i = 0; i < 60 * 60; i++) {
        u32 a = (i % 60 < 4 || i / 60 < 4) ? 0x80 : 0xff;
        u32 c = (u32)(i * 40503u) & 0xff;
        icon[i] = a << 24 | (c * a / 255) << 16 | ((255 - c) * a / 255) << 8 | (a / 2);
    }
    for (int i = 0; i < 256 * 16; i++)
        glyphs[i] = ((i * 7) % 5 == 0) ? 0xffffffffu : (((i * 13) % 3 == 0) ? 0x80808080u : 0);
    j1i_glGenFramebuffers(0, 1, &fbo);
    j1i_glGenTextures(0, 1, &t_screen);
    j1_attach_surface(t_screen, screen, SW, SH, SW);
    j1i_glBindFramebuffer(0, 0x8D40, fbo);
    j1i_glFramebufferTexture2D(0, 0x8D40, 0x8CE0, 0x0DE1, t_screen, 0);
    j1i_glViewport(0, 0, 0, SW, SH);
    for (int i = 0; i < 4; i++)
        j1i_glEnableVertexAttribArray(0, (u32)i);
    t_wall = make_tex(wall, SW, SH);
    t_icon = make_tex(icon, 60, 60);
    t_glyph = make_tex(glyphs, 256, 16);
    prog_tex = make_prog("lowp vec4 s0 = texture2D(texture0, texcoord0_0); gl_FragData[0] = color0 * s0;");
    prog_col = make_prog("gl_FragData[0] = color0;");
    prog_mask = make_prog("lowp vec4 s0 = texture2D(texture0, texcoord0_0); lowp vec4 s1 = texture2D(texture1, texcoord1_0); gl_FragData[0] = color0 * s0 * s1.a;");

    /* Seam check: a half-transparent flat quad blended over black must be uniform; pixels drawn twice (or skipped) on
     * the shared diagonal show up as a second value. */
    for (int mode = 0; mode < 2; mode++) {
        static const float half[4] = {0.5f, 0.5f, 0.5f, 0.5f};
        int odd = 0;
        r_fast = (u32)mode;
        memset(screen, 0, sizeof screen);
        j1i_glEnable(0, 0x0BE2);
        j1i_glBlendFunc(0, 1, 0x303);
        j1i_glUseProgram(0, prog_col);
        quad(30, 40, 90, 100, 0, 0, 1, 1, half);
        for (int y = 40; y < 100; y++)
            for (int x = 30; x < 90; x++)
                if (screen[y * SW + x] != screen[40 * SW + 30]) {
                    if (odd < 3)
                        printf("  seam pixel x=%d y=%d %08x (first pixel %08x)\n", x, y, screen[y * SW + x], screen[40 * SW + 30]);
                    odd++;
                }
        printf("seam check (%s): %d of 3600 pixels differ from the quad's first pixel\n", mode ? "fast" : "reference", odd);
    }
    r_fast = 0;
    memset(screen, 0, sizeof screen);
    frame();
    memcpy(ref, screen, sizeof ref);
    t0 = now();
    for (int f = 0; f < frames; f++) frame();
    tref = (now() - t0) / frames;

    r_fast = 1;
    memset(screen, 0, sizeof screen);
    frame();
    for (int i = 0; i < SW * SH; i++) {
        if (screen[i] != ref[i]) {
            static int shown;
            int pd = 0;
            diff++;
            for (int sh = 0; sh < 32; sh += 8) {
                int d = (int)((screen[i] >> sh) & 255) - (int)((ref[i] >> sh) & 255);
                if (d < 0) d = -d;
                if (d > pd) pd = d;
            }
            if (pd > maxd) maxd = pd;
            if (pd > 2) {
                big++;
                if (shown++ < 12)
                    printf("diff at x=%d y=%d: ref %08x fast %08x\n", i % SW, i / SW, ref[i], screen[i]);
            }
        }
    }
    t0 = now();
    for (int f = 0; f < frames; f++) frame();
    tfast = (now() - t0) / frames;
    printf("reference %.2f ms/frame, fast %.2f ms/frame (%.1fx); %d of %d pixels differ (%d by more than 2), max channel delta %d\n",
           tref * 1e3, tfast * 1e3, tref / tfast, diff, SW * SH, big, maxd);
    return 0;
}
