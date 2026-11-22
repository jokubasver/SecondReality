/*
 * TECHNO (TECHNO/KOE.C, KOEA.ASM, KOEB.ASM - Psi)
 *
 *  1. interference circles: a static 3 plane circle field (odd bitplanes)
 *     and a 1 plane field (even bitplanes) scroll independently; the second
 *     field wobbles per scanline (copper changes BPLCON1/BPL2MOD every
 *     line) and the palettes rotate.
 *  2. white flash, then four columns drop away on the beat.
 *  3. rotating bars with trails: every frame the bars are XOR filled into
 *     a 1 bit frame; the display shows the frames t, t-3, t-6, t-9 as its
 *     four bitplanes, so the colour is the overlap count.
 *  4. the troll picture slides in, shakes and flashes.
 */
#include "kernel.h"

/* flashes and phase starts on row 7 of each group of 8, as the original
 * (one row before the drums on row 0) */
#define BEATROW 7
#include "../../common/xorfill.h"
#include "techno_data.h"

#define LINE0 0x48
#define H 200
#define BW 84                     /* circle field bytes per row (672 px) */
#define PLA (BW * 400)

static u32 *cop[2];
static int cur;

static int sin255(int a) { return muls16(k_sin(a), 255) >> 14; }

/* time in 70 Hz frames */
static u32 tbase;
static int done70;
/* 70 Hz frames since the last call (tbase = start, reset done70 with it) */
static int steps70(void)
{
    int n = (k_frames - tbase) * 7 / 5 - done70;
    done70 += n;
    return n;
}

static void mus_wait_beat(void)
{
    while (!k_exit() && (k_mus_row & 7) != BEATROW)
        k_waitvbl();
}

/* ------------------------------------------------------------------ 1 */
static const u8 pal0[16 * 3] = {
    0,30,40, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
    0,30,40, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0 };
#define G8(v) v, v * 8 / 9, v
#define G7(v) v, v * 7 / 9, v
static const u8 pal1[16 * 3] = {
    G8(30), G8(60), G8(50), G8(40), G8(30), G8(20), G8(10), G8(0),
    G8(30), G8(60), G8(50), G8(40), G8(30), G8(20), G8(10), G8(0) };
static const u8 pal2[16 * 3] = {
    G7(0), G7(10), G7(20), G7(30), G7(40), G7(50), G7(60), G7(30),
    G7(0), G7(10), G7(20), G7(30), G7(40), G7(50), G7(60), G7(30) };

static u16 ipal[16];             /* current 16 colour palette (Amiga format) */

static u16 rgb6(int r, int g, int b)
{
    if (r > 63) r = 63;
    if (g > 63) g = 63;
    if (b > 63) b = 63;
    return ((r >> 2) << 8) | ((g >> 2) << 4) | (b >> 2);
}

/* Amiga index bits: b0=A0 b1=B b2=A1 b3=B b4=A2 -> original A0|A1<<1|A2<<2|B<<3 */
static int orig_index(int i)
{
    return (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4) | ((i & 2) << 2);
}

static void interference_copper(u32 *c, int ax, int ay, int bx, int by, int bpow, int sinurot)
{
    int i, y, X, pw, d, dA;
    u32 a;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0030);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, BW - 42);
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    for (i = 0; i < 32; i++)
        *c++ = CMOVE(R_COLOR00 + i * 2, ipal[orig_index(i)]);
    /* field A in planes 1,3,5 */
    X = ax + 16;
    pw = ((X + 15) >> 4) - 1;
    dA = 16 * pw + 16 - X;
    for (i = 0; i < 3; i++) {
        a = (u32)circ_a + i * PLA + muls16(ay, BW) + pw * 2;
        *c++ = CMOVE(R_BPL1PTH + i * 8, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 8 + 2, a & 0xffff);
    }
    /* field B in planes 2 and 4, one wobble offset per line */
    {
        int xb[H + 1], pwb[H + 1];
        for (y = 0; y <= H; y++) {
            int s = sin255(sinurot + 9 * (y + 1)) >> 3;
            xb[y] = bx + s * bpow / 15 + 16;
            pwb[y] = ((xb[y] + 15) >> 4) - 1;
        }
        a = (u32)circ_b + muls16(by, BW) + pwb[0] * 2;
        for (i = 0; i < 2; i++) {
            *c++ = CMOVE(R_BPL1PTH + 4 + i * 8, a >> 16);
            *c++ = CMOVE(R_BPL1PTH + 4 + i * 8 + 2, a & 0xffff);
        }
        for (y = 0; y < H; y++) {
            d = 16 * pwb[y] + 16 - xb[y];
            if (LINE0 + y == 256) *c++ = CWAIT_PAL;
            *c++ = CWAIT((LINE0 + y) & 0xff, 0x07);
            if (y == 0) *c++ = CMOVE(R_BPLCON0, 0x5200);
            *c++ = CMOVE(R_BPLCON1, dA | (d << 4));
            *c++ = CMOVE(R_BPL2MOD, BW - 42 + (pwb[y + 1] - pwb[y]) * 2);
        }
    }
    *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

static int palanimc, patdir;

static void interference(void)
{
    int frame = 0, k, palfader = 0, scrnrot = 0, overrot = 211, sinurot = 0;
    int sinuspower = 0, powercnt = 0, phase = 0, n;
    cop[0] = k_alloc(800 * 4);
    cop[1] = k_alloc(800 * 4);
    tbase = k_frames; done70 = 0;
    k_mus_frame = 0;
    for (;;) {
        n = steps70();
        while (n-- > 0) {
            if ((k_mus_row & 7) == 0 || (k_mus_row & 7) == 4)
                patdir = -3;
            palanimc += patdir;
            if (palanimc < 0) palanimc = 8 * 3 - 3;
            if (palanimc >= 8 * 3) palanimc = 0;
            sinurot = (sinurot + 7) & 1023;
            if (phase == 0) {
                palfader += 2;
                if (palfader > 512) palfader = 512;
                if (++frame >= 256) {
                    phase = 1;
                    frame = 0;
                }
            } else {
                scrnrot = (scrnrot + 5) & 1023;
                overrot = (overrot + 7) & 1023;
                if (frame >= 70 * 5 && ++powercnt >= 16) {
                    powercnt = 0;
                    if (sinuspower < 15) sinuspower++;
                }
                frame++;
            }
        }
        if (phase == 1 && k_mus_frame >= 925)
            break;
        if (k_exit())
            break;
        if (phase == 0) {
            /* pal0 rotating cyan ring, fading in and then towards white */
            for (k = 0; k < 16; k++) {
                const u8 *p = pal0 + (k & 7) * 3 + palanimc;
                int r = p[0], g = p[1], b = p[2];
                if (palfader <= 256) {
                    r = r * palfader >> 8; g = g * palfader >> 8; b = b * palfader >> 8;
                } else {
                    r += palfader - 256; g += palfader - 256; b += palfader - 256;
                }
                ipal[k] = rgb6(r, g, b);
            }
            cur ^= 1;
            interference_copper(cop[cur], 160, 100, 160, 100, 0, sinurot);
        } else {
            for (k = 0; k < 8; k++) {
                const u8 *p = pal1 + k * 3 + palanimc, *q = pal2 + k * 3 + palanimc;
                ipal[k] = rgb6(p[0], p[1], p[2]);
                ipal[k + 8] = rgb6(q[0], q[1], q[2]);
            }
            cur ^= 1;
            interference_copper(cop[cur],
                                sin255(scrnrot) / 4 + 160, sin255(scrnrot + 256) / 4 + 100,
                                sin255(overrot) / 4 + 160, sin255(overrot + 256) / 4 + 100,
                                sinuspower, sinurot);
        }
        k_set_copper(cop[cur]);
        k_waitvbl();
    }
    /* flash to white */
    {
        static const int f[4] = { 32, 64, 192, 256 };
        u16 base[16];
        memcpy(base, ipal, sizeof(base));
        for (n = 0; n < 4; n++) {
            for (k = 0; k < 16; k++) {
                int r = (base[k] >> 8) & 15, g = (base[k] >> 4) & 15, b = base[k] & 15;
                r = (r * (256 - f[n]) + 15 * f[n]) >> 8;
                g = (g * (256 - f[n]) + 15 * f[n]) >> 8;
                b = (b * (256 - f[n]) + 15 * f[n]) >> 8;
                ipal[k] = (r << 8) | (g << 4) | b;
            }
            cur ^= 1;
            interference_copper(cop[cur], 160, 100, 160, 100, 0, 0);
            k_set_copper(cop[cur]);
            k_waitvbl();
        }
    }
}

/* ------------------------------------------------------------------ 2 */
static u8 *bplane;               /* 1 plane 320x200 */
static u32 simple_cop[32];
static u16 *simple_col;

static void simple_screen(u8 *plane, u16 col0, u16 col1)
{
    u32 *c = simple_cop;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CMOVE(R_BPL1PTH, (u32)plane >> 16);
    *c++ = CMOVE(R_BPL1PTH + 2, (u32)plane & 0xffff);
    *c++ = CMOVE(R_COLOR00, col0);
    simple_col = (u16 *)c;
    *c++ = CMOVE(R_COLOR00 + 2, col1);
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x1200);
    *c++ = CWAIT_PAL;
    *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
    k_set_copper(simple_cop);
}

static void columns(void)
{
    /* grey of colour 15 in the original's ramp: (45,52,60) */
    int b, a;
    const int r0 = 45, g0 = 52, b0 = 60;
    bplane = k_alloc(40 * H);
    memset(bplane, 0xff, 40 * H);
    simple_screen(bplane, 0, rgb6(63, 63, 63));
    while (!k_exit() && k_mus_plus() < -3 && !k_mus_after(24, 61))
        k_waitvbl();
    mus_wait_beat();
    for (b = 0; b < 4 && !k_exit(); b++) {
        int zy = 0, zya = 0, zly, y;
        tbase = k_frames; done70 = 0;
        for (a = 256; a > -400 && !k_exit();) {
            int n = steps70();
            while (n-- > 0 && a > -400) {
                zly = zy;
                zya++;
                zy += zya;
                for (y = zly; y < zy && y < H; y++)
                    memset(bplane + y * 40 + b * 10, 0, 10);
                {
                    int f = a >= 0 ? a : 0;
                    simple_col[1] = rgb6((r0 * (256 - f) + 63 * f) >> 8,
                                         (g0 * (256 - f) + 63 * f) >> 8,
                                         (b0 * (256 - f) + 63 * f) >> 8);
                }
                a -= 32;
            }
            k_waitvbl();
        }
        mus_wait_beat();
        if (b < 3) {
            simple_col[1] = rgb6(63, 63, 63);
            k_waitvbl();
            while (!k_exit() && (k_mus_row & 7) == BEATROW)
                k_waitvbl();
        }
    }
}

/* ------------------------------------------------------------------ 3 */
#define RING 10
static u8 *frames;               /* RING one bit frames */
static int fnum;
static u16 barpal[16][16];
static int curpal;

static void make_barpal(void)
{
    static const u8 lv[5][3] = {
        { 0, 0, 0 }, { 38 * 64 / 111, 33 * 64 / 111, 44 * 64 / 111 },
        { 52 * 64 / 111, 45 * 64 / 111, 58 * 64 / 111 },
        { 67 * 64 / 111, 61 * 64 / 111, 73 * 64 / 111 },
        { 83 * 64 / 111, 77 * 64 / 111, 89 * 64 / 111 } };
    int c, a;
    for (c = 0; c < 16; c++)
        for (a = 0; a < 16; a++) {
            int n = (a & 1) + ((a >> 1) & 1) + ((a >> 2) & 1) + ((a >> 3) & 1);
            int r = lv[n][0] * (10 + c * 9 / 9) / 10;
            int g = lv[n][1] * (10 + c * 7 / 9) / 10;
            int b = lv[n][2] * (10 + c * 5 / 9) / 10;
            barpal[c][a] = rgb6(r, g, b);
        }
}

static u32 *barcop[2];

static void bars_copper(int b, int xshift)
{
    u32 *c = barcop[b];
    int i;
    (void)xshift;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPL2MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    for (i = 0; i < 16; i++)
        *c++ = CMOVE(R_COLOR00 + i * 2, barpal[curpal][i]);
    for (i = 0; i < 4; i++) {
        int f = (fnum - i * 3 + RING * 4) % RING;
        u32 a = (u32)frames + f * 8000;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x4200);
    *c++ = CWAIT_PAL;
    *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

static void draw_bars(int rot, int vm, int wx, int wy)
{
    u8 *f = frames + fnum * 8000;
    int c, hx, hy, vx, vy, x0 = 320, x1 = -1, y0 = 200, y1 = -1;
    k_blit_clear(f, 0, 20, H);
    k_wait_blitter();
    hx = sin255(rot) * 16 * 6 / 5;
    hy = sin255(rot + 256) * 16;
    vx = sin255(rot + 256) * 6 / 5 * vm / 100;
    vy = sin255(rot + 512) * vm / 100;
    for (c = -10; c < 11; c += 2) {
        int cx = vx * c * 2, cy = vy * c * 2, k;
        s16 xs[4], ys[4];
        xs[0] = (-hx - vx + cx) / 16 + wx; ys[0] = (-hy - vy + cy) / 16 + wy;
        xs[1] = (-hx + vx + cx) / 16 + wx; ys[1] = (-hy + vy + cy) / 16 + wy;
        xs[2] = (hx + vx + cx) / 16 + wx;  ys[2] = (hy + vy + cy) / 16 + wy;
        xs[3] = (hx - vx + cx) / 16 + wx;  ys[3] = (hy - vy + cy) / 16 + wy;
        for (k = 0; k < 4; k++) {
            int j = (k + 1) & 3;
            xf_edge(f, 40, 320, xs[k], ys[k], xs[j], ys[j], 0, H);
            if (xs[k] < x0) x0 = xs[k];
            if (xs[k] > x1) x1 = xs[k];
            if (ys[k] < y0) y0 = ys[k];
            if (ys[k] > y1) y1 = ys[k];
        }
    }
    if (x0 < 0) x0 = 0;
    if (x1 > 319) x1 = 319;
    if (y0 < 0) y0 = 0;
    if (y1 > H) y1 = H;
    if (x0 <= x1 && y0 < y1)
        xf_fill(f, 40, x0 >> 4, (x1 >> 4) - (x0 >> 4) + 1, y0, y1 - y0);
    k_wait_blitter();
}

static int lastrow = -1;
static void beat_flash(int n)
{
    if (k_mus_row != lastrow) {
        lastrow = k_mus_row;
        if ((lastrow & 7) == BEATROW) curpal = 15;
    }
    while (n-- > 0 && curpal > 0)
        curpal--;
}

/* doit1/2/3: mode 1 = bouncing spread, 2 = accelerating spin, 3 = + moving */
static void bars(int mode, int count)
{
    int rot = mode == 1 ? 45 : (mode == 2 ? 50 : 45), rota = 10;
    int vm = mode == 1 ? 50 : 100 * 64, vma = 0, rot2 = 0, xpos = 320, xposa = 0;
    tbase = k_frames; done70 = 0;
    while (!k_exit() && count > 0) {
        int n = steps70(), wx = 160, wy = 100, b;
        if (n < 1) {
            k_waitvbl();
            continue;
        }
        count -= n;
        beat_flash(n);
        if (mode == 3) {
            if (k_mus_frame > 2520 && xpos == 0) break;
            if (k_mus_frame > 2520 || count < 333) {
                int r = n;
                while (r--) {
                    xpos -= xposa / 4;
                    if (xpos < 0) xpos = 0;
                    else xposa++;
                }
                if (xpos == 0) break;
            }
            if (rot2 < 32) {
                wx = sin255(rot2) * rot2 / 8 + 160;
                wy = sin255(rot2 + 256) * rot2 / 8 + 100;
            } else {
                wx = sin255(rot2) / 4 + 160;
                wy = sin255(rot2 + 256) / 4 + 100;
            }
            rot2 += 17 * n;
            wx += 320 - xpos;
        }
        fnum = (fnum + 1) % RING;
        draw_bars(rot, mode == 1 ? vm : vm / 64, wx, wy);
        /* the original advances once per drawn frame; scale to time */
        for (b = 0; b < n; b++) {
            if (mode == 1) {
                rot += 2;
                vm += vma;
                if (vm < 25) { vm -= vma; vma = -vma; }
                vma--;
            } else {
                rot += rota / 10;
                vm += vma;
                if (vm < 0) { vm -= vma; vma = -vma; }
                vma--;
                rota++;
            }
        }
        cur ^= 1;
        bars_copper(cur, 0);
        k_set_copper(barcop[cur]);
        k_waitvbl();
    }
}

/* ------------------------------------------------------------------ 4 */
static void troll_part(void)
{
    /* 672 pixel wide buffer: left part black, picture at x 336 */
    u8 *pic = k_alloc(84 * H * 5);
    u32 *c0 = k_alloc(80 * 4);
    u16 cols[32], fade[32];
    int p, y, xpos = 0, xposa = 0, count, ripple = 0, ripplep = 8, c = 0, i;
    for (p = 0; p < 5; p++)
        for (y = 0; y < H; y++)
            memcpy(pic + p * 84 * H + y * 84 + 42, troll + p * 8000 + y * 40, 40);
    for (i = 0; i < 32; i++)
        cols[i] = rgb6(troll_pal[i * 3], troll_pal[i * 3 + 1], troll_pal[i * 3 + 2]);

#define SHOW(xp, colsrc) do {                                                     \
        int X = (xp) + 16, pw = ((X + 15) >> 4) - 1, d = 16 * pw + 16 - X;          \
        u32 *cc = c0;                                                              \
        *cc++ = CMOVE(R_DIWSTRT, 0x2c81); *cc++ = CMOVE(R_DIWSTOP, 0x2cc1);        \
        *cc++ = CMOVE(R_DDFSTRT, 0x0030); *cc++ = CMOVE(R_DDFSTOP, 0x00d0);        \
        *cc++ = CMOVE(R_BPL1MOD, 84 - 42); *cc++ = CMOVE(R_BPL2MOD, 84 - 42);      \
        *cc++ = CMOVE(R_BPLCON1, d | (d << 4)); *cc++ = CMOVE(R_BPLCON0, 0x0200);  \
        for (i = 0; i < 32; i++) *cc++ = CMOVE(R_COLOR00 + i * 2, (colsrc)[i]);    \
        for (i = 0; i < 5; i++) {                                                  \
            u32 a = (u32)pic + i * 84 * H + pw * 2;                                \
            *cc++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);                             \
            *cc++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);                      \
        }                                                                          \
        *cc++ = CWAIT(LINE0, 0x07); *cc++ = CMOVE(R_BPLCON0, 0x5200);              \
        *cc++ = CWAIT_PAL; *cc++ = CWAIT((LINE0 + H) & 0xff, 0x07);                \
        *cc++ = CMOVE(R_BPLCON0, 0x0200); *cc++ = CEND;                            \
        k_set_copper(c0);                                                          \
    } while (0)

    SHOW(0, cols);
    k_waitvbl();
    while (!k_exit() && !(k_mus_order > 35 || (k_mus_order == 35 && k_mus_row > 48)))
        k_waitvbl();
    count = 300;
    tbase = k_frames; done70 = 0;
    while (!k_exit() && count > 0 && xpos != 320) {
        int n = steps70();
        count -= n;
        while (n-- > 0) {
            xpos += xposa / 4;
            if (xpos > 320) xpos = 320;
            else xposa++;
        }
        SHOW(xpos, cols);
        k_waitvbl();
    }
    count = 50;
    while (!k_exit() && count > 0) {
        int n = steps70();
        if (ripplep > 1023) ripplep = 1024;
        else ripplep = ripplep * 5 / 4;
        xpos = 320 + sin255(ripple) / ripplep;
        ripple += ripplep + 100;
        count -= n < 1 ? 1 : n;
        if (c < 16) {
            int add = 45 - c * 3;
            for (i = 0; i < 32; i++)
                fade[i] = rgb6(troll_pal[i * 3] + add, troll_pal[i * 3 + 1] + add, troll_pal[i * 3 + 2] + add);
            c++;
            SHOW(xpos, fade);
        } else
            SHOW(xpos, cols);
        k_waitvbl();
    }
    SHOW(320, cols);
    /* PANICEND continues on this picture: planes, layout and colours */
    {
        u32 *m = (u32 *)k_msg[1];
        u16 *w = (u16 *)(k_msg[1] + 4);
        m[0] = (u32)pic + 42;           /* picture starts at byte 42 */
        w[0] = 84;                      /* bytes per row */
        for (i = 0; i < 26; i++)        /* 6 bit palette of the first 26 colours */
            ;
        memcpy(k_msg[2], troll_pal, 64);
        memcpy(k_msg[3], troll_pal + 64, 32);
    }
    count = 420;
    tbase = k_frames; done70 = 0;
    while (!k_exit() && count > 0) {
        int a = k_mus_plus();
        if (a > -6 && a < 16) break;
        count -= steps70();
        k_waitvbl();
    }
}

void part_main(void)
{
    while (!k_exit() && k_mus_plus() < -4 && !k_mus_after(18, 60))
        k_waitvbl();
    interference();
    columns();
    make_barpal();
    frames = k_alloc(8000 * RING);
    barcop[0] = k_alloc(48 * 4);
    barcop[1] = k_alloc(48 * 4);
    bars(1, 70 * 6);
    bars(2, 70 * 12);
    bars(3, 70 * 14);
    k_set_copper(k_empty_copper);
    troll_part();
}
