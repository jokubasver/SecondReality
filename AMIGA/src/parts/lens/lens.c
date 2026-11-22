/*
 * LNS&ZOOM - the picture is revealed, a glass lens bounces over it, then
 * the picture turns into a rotozoomer (LENS/MAIN.C, ASM.ASM).
 *
 * Amiga version:
 *  - picture in 16 colours, the lens tint adds plane 5 (colours 16..31)
 *  - the lens is drawn in 2x2 pixel cells: chunky gather + movep C2P into a
 *    word interleaved buffer, blitter de-interleave, then a cookie cut with
 *    the 1x1 lens mask (two blits per plane for the line doubling)
 *  - the zoomer runs in 4x4 cells (80x50) with patched displacements, the
 *    copper repeats the lines; the blitter de-interleaves from its interrupt
 *    while the CPU renders the next frame.  Both effects run at 25 fps.
 */
#include "kernel.h"
#include "lens_data.h"

#define BPR 48                  /* lens screen: 2 word margin on each side */
#define PL (BPR * 200)
#define LINE0 0x48
#define ZLINES 4               /* zoomer: 80 x 50 cells of 4x4 pixels */
#define ZROWS 50
#define ZTOP (LINE0 + (200 - ZROWS * ZLINES) / 2)
#define ZPL (40 * ZROWS)

void lens_run(const u8 *src, const u16 *code, const u16 *rows, const u8 *tab, u8 *tmp, int nrows);
void zoom_c2p(const u8 *texmid, const u16 *rowbase, const u8 *tab, u8 *tmp, int nrows);
extern u16 zoom_code[];

static u32 ltab[64], ztab[64];
static u8 *scr[2];
static u32 *cop[2];
static u16 *colp[2];
static u8 *tex;                 /* 256x256 zoomer texture, stored twice */
static int texrow;
static u8 *ltmp, *lplan;
static struct { s16 lx, ly, r0, r1; } old[2];

static void mk_tabs(void)
{
    int j, c, p;
    for (j = 0; j < 4; j++)
        for (c = 0; c < 16; c++) {
            u32 l = 0, z = 0;
            for (p = 0; p < 4; p++)
                if (c & (1 << p)) {
                    l |= (u32)(0xc0 >> (2 * j)) << (24 - 8 * p);
                    z |= (u32)(j == 0 ? 0xf0 : j == 1 ? 0x0f : 0) << (24 - 8 * p);
                }
            ltab[j * 16 + c] = l;
            ztab[j * 16 + c] = z;
        }
}

/* builds the zoomer texture a few rows per call; returns 1 when done */
static int tex_step(int n)
{
    while (n-- > 0 && texrow < 256) {
        int y = texrow, x;
        int a = y * 10 / 11 - 18;
        const u8 *s;
        u8 *d = tex + (y << 8);
        if (a < 0 || a > 199) a = 0;
        s = chunky + (PAD + a) * 320 + 32;
        for (x = 0; x < 256; x++)
            d[x] = d[x + 65536] = s[x];
        texrow++;
    }
    return texrow >= 256;
}

static u32 *mk_cop(u32 *c, u8 *s, int npl, int plsize, int xoff, int mod, u16 **cp, int top)
{
    int i;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, mod);
    *c++ = CMOVE(R_BPL2MOD, mod);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *cp = (u16 *)c;
    for (i = 0; i < 32; i++)
        *c++ = CMOVE(R_COLOR00 + i * 2, 0);
    for (i = 0; i < npl; i++) {
        u32 a = (u32)s + i * plsize + xoff;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CWAIT(top, 0x07);
    *c++ = CMOVE(R_BPLCON0, (npl << 12) | 0x0200);
    return c;
}

static void cop_end(u32 *c)
{
    *c++ = CWAIT_PAL;
    *c++ = CWAIT((LINE0 + 200) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

static void set_col(int i, int r, int g, int b)
{
    u16 v = ((r >> 2) << 8) | ((g >> 2) << 4) | (b >> 2);
    colp[0][i * 2 + 1] = v;
    colp[1][i * 2 + 1] = v;
}

/* lens tint fade: x = 0 (only the tint) .. 31 (full) */
static void set_pal(int x)
{
    int i, k;
    for (i = 0; i < 16; i++) {
        const u8 *b = pal + i * 3, *t = pal + 48 + i * 3;
        int c[3];
        set_col(i, b[0], b[1], b[2]);
        for (k = 0; k < 3; k++)
            c[k] = t[k] - b[k] * (31 - x) / 31;
        set_col(16 + i, c[0], c[1], c[2]);
    }
}

/* ---- part 1: reveal ----------------------------------------------------- */
static s16 curl[200], curr[200];

static void copy_span(int y, int a, int b)
{
    int p, x;
    if (a < 0) a = 0;
    if (b > 320) b = 320;
    for (x = a & ~7; x < b; x += 8) {
        int lo = x < a ? a - x : 0, hi = x + 8 > b ? b - x : 8;
        u8 m = (0xff >> lo) & (u8)(0xff << (8 - hi));
        for (p = 0; p < 4; p++) {
            u8 *d = scr[0] + p * PL + y * BPR + 4 + (x >> 3);
            *d = (*d & ~m) | (pic[p * 8000 + y * 40 + (x >> 3)] & m);
        }
    }
}

static void reveal(int f)
{
    int y, steps = (f < 80 ? f : 80) * 6;
    for (y = 0; y < 200; y++) {
        s32 st = 170 * 64 + (100 - y) * 50;
        int r = (st + steps * ((19 + y / 5 + 4) & ~7)) >> 6;
        int l = (st - steps * ((19 + (199 - y) / 5 + 4) & ~7)) >> 6;
        if (r > 320) r = 320;
        if (l < 0) l = 0;
        if (r > curr[y]) copy_span(y, curr[y], r);
        if (l < curl[y]) copy_span(y, l, curl[y]);
        curr[y] = r;
        curl[y] = l;
    }
}

/* ---- part 2: lens -------------------------------------------------------- */
static void restore(int b)
{
    int p, w0, w1, h, r0 = old[b].r0, r1 = old[b].r1, ly = old[b].ly, wx;
    if (r1 <= r0) return;
    wx = old[b].lx >> 4;
    w0 = wx < 0 ? 0 : wx;
    w1 = wx + 10 > 19 ? 19 : wx + 10;
    h = r1 - r0;
    for (p = 0; p < 4; p++) {
        k_wait_blitter();
        BLTCON0 = 0x09f0;
        BLTCON1 = 0;
        BLTAFWM = 0xffff;
        BLTALWM = 0xffff;
        BLTAPT = (u32)(pic + p * 8000 + (ly + r0) * 40 + w0 * 2);
        BLTDPT = (u32)(scr[b] + p * PL + (ly + r0) * BPR + 4 + w0 * 2);
        BLTAMOD = 40 - 2 * (w1 - w0 + 1);
        BLTDMOD = BPR - 2 * (w1 - w0 + 1);
        BLTSIZE = (h << 6) | (w1 - w0 + 1);
    }
    k_wait_blitter();
    BLTCON0 = 0x0100;
    BLTDPT = (u32)(scr[b] + 4 * PL + (ly + r0) * BPR + 4 + wx * 2);
    BLTDMOD = BPR - 22;
    BLTSIZE = (h << 6) | 11;
    old[b].r1 = 0;
}

static void draw_lens(int b, int x, int y)
{
    int lx = x - LENSW / 2, ly = y - LENSH / 2;
    int r0 = ly < 0 ? -ly : 0, r1 = 200 - ly > LENSH ? LENSH : 200 - ly;
    int cy0, n, p, k, s, wx;
    restore(b);
    if (r1 <= r0) return;
    cy0 = r0 >> 1;
    n = ((r1 - 1) >> 1) - cy0 + 1;
    /* (the restore blits run meanwhile) */
    lens_run(chunky + (PAD + y) * 320 + x, lcode, lrows + cy0, (u8 *)ltab + 128,
             ltmp + cy0 * 80, n);
    for (p = 0; p < 4; p++) {
        k_wait_blitter();
        BLTCON0 = 0x09f0;
        BLTCON1 = 0;
        BLTAFWM = 0xffff;
        BLTALWM = 0xffff;
        BLTAPT = (u32)(ltmp + cy0 * 80 + p * 2);
        BLTDPT = (u32)(lplan + p * (CROWS * 20) + cy0 * 20);
        BLTAMOD = 6;
        BLTDMOD = 0;
        BLTSIZE = ((n * 10) << 6) | 1;
    }
    s = lx & 15;
    wx = lx >> 4;
    for (p = 0; p < 4; p++)
        for (k = 0; k < 2; k++) {
            int r = r0 + k, cnt = (r1 - r + 1) >> 1;
            u8 *d = scr[b] + p * PL + (ly + r) * BPR + 4 + wx * 2;
            if (cnt <= 0) continue;
            k_wait_blitter();
            BLTCON0 = 0x0fca | (s << 12);
            BLTCON1 = s << 12;
            BLTAPT = (u32)(lmask + r * 11);
            BLTBPT = (u32)(lplan + p * (CROWS * 20) + (r >> 1) * 20);
            BLTCPT = (u32)d;
            BLTDPT = (u32)d;
            BLTAMOD = 22;
            BLTBMOD = -2;
            BLTCMOD = 2 * BPR - 22;
            BLTDMOD = 2 * BPR - 22;
            BLTSIZE = (cnt << 6) | 11;
        }
    {
        u8 *d = scr[b] + 4 * PL + (ly + r0) * BPR + 4 + wx * 2;
        k_wait_blitter();
        BLTCON0 = 0x0bfa | (s << 12);
        BLTCON1 = 0;
        BLTAPT = (u32)(lmask + r0 * 11);
        BLTCPT = (u32)d;
        BLTDPT = (u32)d;
        BLTAMOD = 0;
        BLTCMOD = BPR - 22;
        BLTDMOD = BPR - 22;
        BLTSIZE = ((r1 - r0) << 6) | 11;
    }
    old[b].lx = lx;
    old[b].ly = ly;
    old[b].r0 = r0;
    old[b].r1 = r1;
}

/* ---- part 3: zoomer ------------------------------------------------------ */
static u16 rowbase[ZROWS];

void zoom_patch(u32 u, u32 v, s32 du, s32 dv);
void zoom_rows(u32 x, u32 y, s32 dx, s32 dy, u16 *out, int n);

static void zoom_setup(int x, int y, int xa, int ya)
{
    s32 xadd = (s32)xa << 6, yadd = (s32)ya << 6;
    /* cell k samples column 2k+1 of the original 160 */
    zoom_patch(yadd, -xadd, yadd * 2, -xadd * 2);
    k_flush_cache();
    /* aspect ratio *307/256, rows are ZLINES lines instead of 2 */
    xadd = (xadd * (307 * ZLINES / 2)) >> 8;
    yadd = (yadd * (307 * ZLINES / 2)) >> 8;
    zoom_rows((u32)x << 16, (u32)y << 16, xadd, yadd, rowbase, ZROWS);
}

static void zoom_pal(int add, int white)  /* brighten by add, fade to white 0..63 */
{
    static int la = -1, lw = -1;
    int i, k;
    if (add == la && white == lw) return;
    la = add;
    lw = white;
    for (i = 0; i < 16; i++) {
        int c[3];
        for (k = 0; k < 3; k++) {
            int v = pal[i * 3 + k] + add;
            if (v > 63) v = 63;
            c[k] = (v * (63 - white) + white * 63) / 63;
        }
        set_col(i, c[0], c[1], c[2]);
    }
}

/* the 8 de-interleave blits are chained from the blitter interrupt */
static u8 *dq_tmp, *dq_dst;
static volatile int dq_n = 8;

static void dq_next(void)
{
    int i, h;
    if (dq_n >= 8) return;
    i = dq_n >> 1;
    h = dq_n & 1;
    dq_n++;
    BLTCON0 = 0x09f0;
    BLTCON1 = 0;
    BLTAFWM = 0xffff;
    BLTALWM = 0xffff;
    BLTAPT = (u32)(dq_tmp + i * 2 + h * (ZROWS / 2) * 160);
    BLTDPT = (u32)(dq_dst + i * ZPL + h * (ZROWS / 2) * 40);
    BLTAMOD = 6;
    BLTDMOD = 0;
    BLTSIZE = (((ZROWS / 2) * 20) << 6) | 1;
}

static void deinterleave(u8 *tmp, u8 *dst)
{
    k_wait_blitter();
    dq_tmp = tmp;
    dq_dst = dst;
    k_set_ipl(3);
    dq_n = 0;
    dq_next();
    k_set_ipl(0);
}

static void dq_wait(void)
{
    while (dq_n < 8) ;
    k_wait_blitter();
}

static void part3(void)
{
    u8 *zs[2], *tmp[2];
    u32 *c;
    int b = 0, i, r, frame, started = 0;
    u32 t0;
    k_set_copper(k_empty_copper);
    k_waitvbl();
    k_wait_blitter();
    k_free(scr[0]);
    k_free(scr[1]);
    while (!tex_step(64)) ;
    for (i = 0; i < 2; i++) {
        zs[i] = k_alloc(ZPL * 4);
        tmp[i] = k_alloc(ZROWS * 160);
        cop[i] = k_alloc((60 + ZROWS * ZLINES * 3) * 4);
        c = mk_cop(cop[i], zs[i], 4, ZPL, 0, 0, &colp[i], ZTOP);
        for (r = 0; r < ZROWS * ZLINES; r++) {
            int l = ZTOP + r, m = (r % ZLINES) == ZLINES - 1 ? 0 : -40;
            if (l == 256) *c++ = CWAIT_PAL;
            *c++ = CWAIT(l & 0xff, 0x07);
            *c++ = CMOVE(R_BPL1MOD, m);
            *c++ = CMOVE(R_BPL2MOD, m);
        }
        *c++ = CWAIT((ZTOP + ZROWS * ZLINES) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
    }
    zoom_pal(75, 0);
    k_blit_hook = dq_next;
    INTENA = INTF_SETCLR | INTF_BLIT;
    t0 = k_frames;
    /* pipelined: the blitter de-interleaves frame n while the CPU renders
     * frame n+1 into the other buffer */
    for (;;) {
        frame = (k_frames - t0) * 7 / 5;
        if (frame >= PATH2N || k_exit() || k_mus_plus() > -4) break;
        zoom_setup(path2[frame * 4], path2[frame * 4 + 1], path2[frame * 4 + 2], path2[frame * 4 + 3]);
        zoom_c2p(tex + 32768, rowbase, (u8 *)ztab + 128, tmp[b], ZROWS);
        dq_wait();
        if (started) {
            k_set_copper(cop[b ^ 1]);
            k_waitvbl();
        }
        if (frame < 16)
            zoom_pal((15 - frame) * 5, 0);
        else if (frame > PATH2N - 128) {
            int a = (frame - (PATH2N - 128)) / 2;
            zoom_pal(0, a > 63 ? 63 : a);
        } else
            zoom_pal(0, 0);
        deinterleave(tmp[b], zs[b]);
        started = 1;
        b ^= 1;
    }
    dq_wait();
    for (i = 0; i < 16; i++)
        set_col(i, 63, 63, 63);
    k_waitvbl();
}

void part_main(void)
{
    u32 *c;
    int i, b, f;
    u32 t0;
    mk_tabs();
    /* MNTSCRL has faded out: release its memory before allocating */
    k_set_copper(k_empty_copper);
    tex = k_alloc(131072);
    for (i = 0; i < 2; i++) {
        scr[i] = k_alloc(PL * 5);
        cop[i] = k_alloc(64 * 4);
        c = mk_cop(cop[i], scr[i], 5, PL, 4, BPR - 40, &colp[i], LINE0);
        cop_end(c);
    }
    ltmp = k_alloc(CROWS * 80);
    lplan = k_alloc(CROWS * 80);
    for (i = 0; i < 200; i++)
        curl[i] = curr[i] = (170 * 64 + (100 - i) * 50) >> 6;
    set_pal(31);
    k_set_copper(cop[0]);
    /* part 1: wait for the music, then reveal the picture */
    while (!k_exit() && k_mus_plus() < -6 && !k_mus_after(45, 58)) {
        tex_step(8);
        k_waitvbl();
    }
    k_waitvbl();
    t0 = k_frames;
    while (!k_exit() && (f = (k_frames - t0) * 7 / 5) < 300) {
        if (f <= 81) reveal(f);
        tex_step(8);
        k_waitvbl();
    }
    reveal(80);
    while (!k_exit() && k_mus_plus() < -20 && !k_mus_after(48, 44)) {
        tex_step(8);
        k_waitvbl();
    }
    memcpy(scr[1], scr[0], PL * 5);
    /* part 2: the lens */
    k_waitvbl();
    t0 = k_frames;
    b = 1;
    for (;;) {
        f = (k_frames - t0) * 7 / 5;
        if (f >= PATH1N || k_exit()) break;
        if (f < 96) {
            int a = (f - 32) / 2;
            set_pal(a < 0 ? 0 : a > 31 ? 31 : a);
        }
        draw_lens(b, path1[f * 2], path1[f * 2 + 1]);
        k_wait_blitter();
        k_set_copper(cop[b]);
        k_waitvbl();
        b ^= 1;
    }
    while (!k_exit() && (k_frames - t0) * 7 / 5 < 720)
        k_waitvbl();
    part3();
}
