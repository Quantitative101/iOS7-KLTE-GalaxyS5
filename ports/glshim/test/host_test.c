/* Native (Mac) test for the software GLES2 renderer in impl.c/render.h. Build and run: glshim/test/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../impl.c"

void j1_logstr(const char *tag, const char *s) { (void)tag; (void)s; }
void j1_logbuf(const char *tag, const char *s, int n) { (void)tag; (void)s; (void)n; }

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define W 64
#define H 64
static u32 surf[W * H];

static const float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static void setup(u32 *fbo, u32 *tex)
{
    j1i_glGenFramebuffers(0, 1, fbo);
    j1i_glGenTextures(0, 1, tex);
    j1_attach_surface(*tex, surf, W, H, W);
    j1i_glBindFramebuffer(0, 0x8D40, *fbo);
    j1i_glFramebufferTexture2D(0, 0x8D40, 0x8CE0, 0x0DE1, *tex, 0);
    j1i_glViewport(0, 0, 0, W, H);
}

static u32 make_prog(int textured)
{
    u32 fs = j1i_glCreateShader(0, 0x8B30), prog = j1i_glCreateProgram(0);
    const char *src = textured ? "gl_FragData[0] = texture2D(tex0, texcoord0_0) * color0;" : "gl_FragData[0] = color0;";
    s32 len = (s32)strlen(src);
    j1i_glShaderSource(0, fs, 1, &src, &len);
    j1i_glAttachShader(0, prog, fs);
    j1i_glBindAttribLocation(0, prog, 0, "vertex_position");
    j1i_glBindAttribLocation(0, prog, 1, "vertex_color0");
    j1i_glBindAttribLocation(0, prog, 2, "vertex_texcoord0");
    j1i_glLinkProgram(0, prog);
    return prog;
}

int main(void)
{
    u32 fbo, tex, prog;
    setup(&fbo, &tex);

    /* 0. pixel unpacking: 3x2 ALPHA texture, rows padded to 4 bytes (default GL_UNPACK_ALIGNMENT), then tight (align 1) */
    {
        u32 t2;
        static const u8 padded[8] = {10, 20, 30, 0xEE, 40, 50, 60, 0xEE};
        static const u8 tight[6] = {11, 21, 31, 41, 51, 61};
        j1i_glGenTextures(0, 1, &t2);
        j1i_glBindTexture(0, 0x0DE1, t2);
        j1i_glTexImage2D(0, 0x0DE1, 0, 0x1906, 3, 2, 0, 0x1906, 0x1401, padded);
        CHECK(r_tex[t2].px[3] == (40u << 24) && r_tex[t2].px[5] == (60u << 24),
              "unpack align 4: row1 = %08x %08x", r_tex[t2].px[3], r_tex[t2].px[5]);
        j1i_glPixelStorei(0, 0x0CF5, 1);
        j1i_glTexImage2D(0, 0x0DE1, 0, 0x1906, 3, 2, 0, 0x1906, 0x1401, tight);
        CHECK(r_tex[t2].px[3] == (41u << 24) && r_tex[t2].px[5] == (61u << 24),
              "unpack align 1: row1 = %08x %08x", r_tex[t2].px[3], r_tex[t2].px[5]);
        j1i_glTexSubImage2D(0, 0x0DE1, 0, 1, 1, 2, 1, 0x1906, 0x1401, tight);
        CHECK(r_tex[t2].px[4] == (11u << 24) && r_tex[t2].px[5] == (21u << 24),
              "subimage align 1: %08x %08x", r_tex[t2].px[4], r_tex[t2].px[5]);
        j1i_glPixelStorei(0, 0x0CF5, 4);
        /* APPLE_row_bytes: 3x2 ALPHA with 4-byte rows given as an explicit byte pitch (alignment 1 must not matter) */
        j1i_glPixelStorei(0, 0x0CF5, 1);
        j1i_glPixelStorei(0, 0x8A16, 4);
        j1i_glTexImage2D(0, 0x0DE1, 0, 0x1906, 3, 2, 0, 0x1906, 0x1401, padded);
        CHECK(r_tex[t2].px[3] == (40u << 24) && r_tex[t2].px[5] == (60u << 24),
              "unpack row bytes 4: row1 = %08x %08x", r_tex[t2].px[3], r_tex[t2].px[5]);
        j1i_glPixelStorei(0, 0x8A16, 0);
        j1i_glPixelStorei(0, 0x0CF5, 4);
        /* BGRA from CA: 3 px wide = 12 bytes per row, rows padded to 32 bytes (CGImage) */
        {
            static u8 bgra[2 * 32];
            for (int i = 0; i < 64; i++)
                bgra[i] = 0xEE;
            for (int i = 0; i < 12; i++) {
                bgra[i] = (u8)(1 + i);
                bgra[32 + i] = (u8)(21 + i);
            }
            j1i_glTexImage2D(0, 0x0DE1, 0, 0x1908, 3, 2, 0, 0x80E1, 0x1401, bgra);
            CHECK(((r_tex[t2].px[3] >> 24) & 0xff) == 24 && ((r_tex[t2].px[5] >> 24) & 0xff) == 32,
                  "bgra rows padded to 32 bytes: row1 = %08x %08x", r_tex[t2].px[3], r_tex[t2].px[5]);
        }
        j1i_glBindTexture(0, 0x0DE1, tex);
    }

    /* 0b. uniforms are per program: setting texmat0 on one program must not change another's */
    {
        u32 pa = j1i_glCreateProgram(0), pb = j1i_glCreateProgram(0);
        s32 loc = j1i_glGetUniformLocation(0, pa, "texmat0");
        const r_uni_t *ua, *ub;
        j1i_glUseProgram(0, pa);
        j1i_glUniform4f(0, loc, 1.0f, 2.0f, 3.0f, 4.0f);
        j1i_glUseProgram(0, pb);
        j1i_glUniform4f(0, loc, 5.0f, 6.0f, 7.0f, 8.0f);
        j1i_glUseProgram(0, pa);
        ua = r_uni_find((u32)loc, 0);
        j1i_glUseProgram(0, pb);
        ub = r_uni_find((u32)loc, 0);
        CHECK(ua && ub && ua->f[0] == 1.0f && ub->f[0] == 5.0f, "per-program uniforms: a=%g b=%g",
              ua ? ua->f[0] : -1.0f, ub ? ub->f[0] : -1.0f);
        j1i_glUseProgram(0, 0);
    }

    /* 1. clear */
    j1i_glClearColor(0, 1.0f, 0.0f, 0.0f, 1.0f);
    j1i_glClear(0, 0x4000);
    CHECK(surf[0] == 0xFFFF0000u, "clear px0=%08x", surf[0]);
    CHECK(surf[W * H - 1] == 0xFFFF0000u, "clear last=%08x", surf[W * H - 1]);

    /* 2. scissored clear */
    j1i_glEnable(0, 0x0C11);
    j1i_glScissor(0, 8, 8, 16, 16);
    j1i_glClearColor(0, 0, 0, 1.0f, 1.0f);
    j1i_glClear(0, 0x4000);
    CHECK(surf[10 * W + 10] == 0xFF0000FFu, "scissor inside=%08x", surf[10 * W + 10]);
    CHECK(surf[2 * W + 2] == 0xFFFF0000u, "scissor outside=%08x", surf[2 * W + 2]);
    j1i_glDisable(0, 0x0C11);

    /* 3. indexed quad covering the viewport, per-vertex colour */
    prog = make_prog(0);
    j1i_glUseProgram(0, prog);
    {
        float verts[4 * 2] = {-1, -1, 1, -1, 1, 1, -1, 1};
        float cols[4 * 4] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        u16 idx[6] = {0, 1, 2, 0, 2, 3};
        float half[16] = {0.5f, 0, 0, 0, 0, 0.5f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        float red[4 * 4] = {1, 0, 0, 0.5f, 1, 0, 0, 0.5f, 1, 0, 0, 0.5f, 1, 0, 0, 0.5f};
        u32 p;
        j1i_glUniformMatrix4fv(0, (s32)r_uni_hash("vertex_matrix"), 1, 0, ident);
        j1i_glVertexAttribPointer(0, 0, 2, 0x1406, 0, 0, verts);
        j1i_glEnableVertexAttribArray(0, 0);
        j1i_glVertexAttribPointer(0, 1, 4, 0x1406, 0, 0, cols);
        j1i_glEnableVertexAttribArray(0, 1);
        j1i_glDrawElements(0, 4, 6, 0x1403, idx);
        CHECK(surf[0] == 0xFFFFFFFFu, "quad px0=%08x", surf[0]);
        CHECK(surf[W * H - 1] == 0xFFFFFFFFu, "quad last=%08x", surf[W * H - 1]);
        CHECK(surf[31 * W + 31] == 0xFFFFFFFFu, "quad shared-edge px=%08x", surf[31 * W + 31]);

        /* 4. half-size quad (matrix scale 0.5), red at 50% alpha, SRC_ALPHA blend over white */
        j1i_glUniformMatrix4fv(0, (s32)r_uni_hash("vertex_matrix"), 1, 0, half);
        j1i_glVertexAttribPointer(0, 1, 4, 0x1406, 0, 0, red);
        j1i_glEnable(0, 0x0BE2);
        j1i_glBlendFunc(0, 0x302, 0x303);
        j1i_glDrawElements(0, 4, 6, 0x1403, idx);
        p = surf[32 * W + 32];
        CHECK(surf[0] == 0xFFFFFFFFu, "outside half quad=%08x", surf[0]);
        CHECK(((p >> 16) & 255) == 255 && ((p >> 8) & 255) >= 126 && ((p >> 8) & 255) <= 129,
              "blend center=%08x (want R=ff G~80)", p);
        j1i_glDisable(0, 0x0BE2);
    }

    /* 5. VBO + DrawArrays triangle strip, constant colour from a disabled array */
    {
        u32 bufs[2];
        float verts[4 * 2] = {-1, -1, 1, -1, -1, 1, 1, 1};
        j1i_glGenBuffers(0, 2, bufs);
        j1i_glBindBuffer(0, 0x8892, bufs[0]);
        j1i_glBufferData(0, 0x8892, sizeof verts, verts, 0x88E4);
        j1i_glUniformMatrix4fv(0, (s32)r_uni_hash("vertex_matrix"), 1, 0, ident);
        j1i_glVertexAttribPointer(0, 0, 2, 0x1406, 0, 0, (const void *)0);
        j1i_glEnableVertexAttribArray(0, 0);
        j1i_glDisableVertexAttribArray(0, 1);
        j1i_glVertexAttrib4f(0, 1, 0.0f, 0.0f, 1.0f, 1.0f);
        j1i_glDrawArrays(0, 5, 0, 4);
        CHECK(surf[5 * W + 5] == 0xFF0000FFu, "vbo strip const colour=%08x", surf[5 * W + 5]);
        CHECK(surf[W * H - 1] == 0xFF0000FFu, "vbo strip last=%08x", surf[W * H - 1]);
        j1i_glBindBuffer(0, 0x8892, 0);
    }

    /* 6. textured quad, 2x2 nearest */
    {
        u32 t2, prog2 = make_prog(1);
        u8 texels[2 * 2 * 4] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
        float verts[4 * 2] = {-1, -1, 1, -1, -1, 1, 1, 1};
        float uv[4 * 2] = {0, 0, 1, 0, 0, 1, 1, 1};
        float col[4 * 4] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        j1i_glUseProgram(0, prog2);
        j1i_glGenTextures(0, 1, &t2);
        j1i_glActiveTexture(0, 0x84C0);
        j1i_glBindTexture(0, 0x0DE1, t2);
        j1i_glTexImage2D(0, 0x0DE1, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401, texels);
        j1i_glVertexAttribPointer(0, 0, 2, 0x1406, 0, 0, verts);
        j1i_glEnableVertexAttribArray(0, 0);
        j1i_glVertexAttribPointer(0, 1, 4, 0x1406, 0, 0, col);
        j1i_glEnableVertexAttribArray(0, 1);
        j1i_glVertexAttribPointer(0, 2, 2, 0x1406, 0, 0, uv);
        j1i_glEnableVertexAttribArray(0, 2);
        j1i_glUniform4f(0, (s32)r_uni_hash("texmat0"), 1.0f, 1.0f, 0.0f, 0.0f);
        j1i_glDrawArrays(0, 5, 0, 4);
        CHECK(surf[4 * W + 4] == 0xFFFF0000u, "tex bottom-left red=%08x", surf[4 * W + 4]);
        CHECK(surf[4 * W + 60] == 0xFF00FF00u, "tex bottom-right green=%08x", surf[4 * W + 60]);
        CHECK(surf[60 * W + 4] == 0xFF0000FFu, "tex top-left blue=%08x", surf[60 * W + 4]);
        CHECK(surf[60 * W + 60] == 0xFFFFFFFFu, "tex top-right white=%08x", surf[60 * W + 60]);

        /* 6b. CA's mask shader: color0 * s0 * s1.a with the mask on unit 1 through vertex_texcoord1/texmat1 */
        {
            u32 fs = j1i_glCreateShader(0, 0x8B30), pm = j1i_glCreateProgram(0), tm;
            const char *src = "void main(void) {\n  lowp vec4 s0 = texture2D(texture0, texcoord0_0);\n"
                              "  lowp vec4 s1 = texture2D(texture1, texcoord1_0);\n  gl_FragData[0] = color0 * s0 * s1.a;\n}\n";
            s32 len = (s32)strlen(src);
            u8 mask[4] = {0, 0, 0, 128};
            u32 got, r, a;
            j1i_glShaderSource(0, fs, 1, &src, &len);
            j1i_glAttachShader(0, pm, fs);
            j1i_glBindAttribLocation(0, pm, 0, "vertex_position");
            j1i_glBindAttribLocation(0, pm, 1, "vertex_color0");
            j1i_glBindAttribLocation(0, pm, 2, "vertex_texcoord0");
            j1i_glBindAttribLocation(0, pm, 3, "vertex_texcoord1");
            j1i_glLinkProgram(0, pm);
            CHECK(r_prog[pm].frag_tex == R_FM_MASK && r_prog[pm].tc1 == 3, "mask program mode=%u tc1=%d",
                  r_prog[pm].frag_tex, r_prog[pm].tc1);
            j1i_glUseProgram(0, pm);
            j1i_glGenTextures(0, 1, &tm);
            j1i_glActiveTexture(0, 0x84C1);
            j1i_glBindTexture(0, 0x0DE1, tm);
            j1i_glTexImage2D(0, 0x0DE1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, mask);
            j1i_glActiveTexture(0, 0x84C0);
            j1i_glVertexAttribPointer(0, 3, 2, 0x1406, 0, 0, uv);
            j1i_glEnableVertexAttribArray(0, 3);
            j1i_glUniformMatrix4fv(0, (s32)r_uni_hash("vertex_matrix"), 1, 0, ident);
            j1i_glUniform4f(0, (s32)r_uni_hash("texmat0"), 1.0f, 1.0f, 0.0f, 0.0f);
            j1i_glUniform4f(0, (s32)r_uni_hash("texmat1"), 1.0f, 1.0f, 0.0f, 0.0f);
            j1i_glEnable(0, 0x0C11);           /* keep the bottom-left texel for the readback test below */
            j1i_glScissor(0, 32, 32, 32, 32);
            j1i_glDrawArrays(0, 5, 0, 4);
            j1i_glDisable(0, 0x0C11);
            got = surf[60 * W + 60];           /* white texel * mask alpha 128/255 */
            r = (got >> 16) & 255;
            a = got >> 24;
            CHECK(r >= 127 && r <= 129 && a >= 127 && a <= 129 && ((got >> 8) & 255) == r && (got & 255) == r,
                  "mask white*0.5=%08x", got);
            CHECK(surf[4 * W + 4] == 0xFFFF0000u, "mask draw stayed inside scissor=%08x", surf[4 * W + 4]);
            j1i_glDisableVertexAttribArray(0, 3);
        }
    }

    /* 7. readback and the small query stubs */
    {
        u8 px[4];
        s32 pr[2], plen = 7;
        char log[8] = "xxxxxxx";
        j1i_glReadPixels(0, 4, 4, 1, 1, 0x1908, 0x1401, px);   /* texel bottom-left of the textured quad: red */
        CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255, "readpixels rgba=%02x%02x%02x%02x", px[0], px[1], px[2], px[3]);
        j1i_glReadPixels(0, 4, 4, 1, 1, 0x80E1, 0x1401, px);   /* BGRA order */
        CHECK(px[0] == 0 && px[2] == 255, "readpixels bgra=%02x%02x%02x%02x", px[0], px[1], px[2], px[3]);
        j1i_glGetShaderPrecisionFormat(0, 0x8B30, 0x8DF2, pr, &plen);
        CHECK(pr[0] == 127 && plen == 23, "precision %d %d", pr[0], plen);
        j1i_glGetShaderInfoLog(0, 1, 8, &plen, log);
        CHECK(plen == 0 && log[0] == 0, "infolog");
        CHECK(j1i_glClientWaitSync(0, j1i_glFenceSync(0, 0x9117, 0), 0, 0, 0) == 0x911A, "sync");
    }

    printf(fails ? "RESULT: %d FAILED\n" : "RESULT: all passed (%d failures)\n", fails);
    return fails != 0;
}
