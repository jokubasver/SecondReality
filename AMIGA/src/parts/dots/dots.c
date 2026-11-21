/*
 * MINVBALL - balls fountaining and bouncing on a floor (DOTS/MAIN.C, ASM.ASM).
 *
 * Amiga version: 5 interleaved bitplanes; plane 0 holds the ball centres
 * and the shadows, planes 1-4 the depth shade of the ball, so a ball costs
 * a few long ORs.  The floor gradient is COLOR00 changed by the copper.
 * Triple buffered: the blitter clears the last shown screen while the CPU
 * draws the next.  The physics runs at the original 70 Hz.
 */
#include "kernel.h"
#include "dots_data.h"

#define DOTN 384                /* the original has 512 */
#define LINE0 0x48
#define ROW 210                 /* 5 planes x 42 bytes */
#define NROWS 202
#define BUFSZ (ROW * NROWS)

void dots_draw(const s16 *dots, int n, u8 *buf, int rotsin, int rotcos,
               const s16 *inv, const u32 *pat, const u32 *rowoff, const u8 *lev);

static s16 dot[DOTN][4];        /* x, y, z, yadd */
static u16 dottaul[DOTN];
static u32 pat[48];
static u32 rowoff[NROWS];
static u8 lev[256];
static u8 *buf[3];
static u32 *cop[3];
static u16 *colp[3], *floorp[3];
static u8 pal[132 * 3];         /* 0..31 colours, 32..131 floor lines */
static u32 seed = 1;

static int rnd(void)            /* MSC rand() */
{
    seed = seed * 214013 + 2531011;
    return (seed >> 16) & 0x7fff;
}

static int isin(int d) { return dsin[d & 1023]; }
static int icos(int d) { return dsin[(d + 256) & 1023]; }

static void setpal(const u8 *p)
{
    int i, b;
    for (i = 0; i < 132; i++) {
        u16 v = ((p[i * 3] >> 2) << 8) | ((p[i * 3 + 1] >> 2) << 4) | (p[i * 3 + 2] >> 2);
        for (b = 0; b < 3; b++) {
            if (i < 32)
                colp[b][i * 2 + 1] = v;
            else
                floorp[b][(i - 32) * 4 + 3] = v;
        }
    }
}

void part_main(void)
{
    static const u8 cols[4][3] = {{0, 0, 0}, {4, 25, 30}, {8, 40, 45}, {16, 55, 60}};
    u8 pal2[132 * 3];
    u32 *c;
    int a, b, i, j = 0, f = 0, frame = 0, cur = 0;
    int dropper = 22000, grav = 3, gravd = 13, rot = 0, rots = 0, rota = -64;
    int rotsin = 0, rotcos = 0, gravity = 3, gravityd = 13;
    u32 t0, ph;
    /* tables */
    for (a = 0; a < 16; a++) {
        pat[a] = 0x60000000u >> a;
        pat[16 + a] = 0xf0000000u >> a;
        pat[32 + a] = 0xc0000000u >> a;
    }
    for (a = 0; a < NROWS; a++)
        rowoff[a] = a * ROW;
    for (a = 0; a < 256; a++) {
        int cc = a - (43 + 20) / 2;
        cc = cc * 3 / 4 + 8;
        if (cc < 0) cc = 0; else if (cc > 15) cc = 15;
        cc = 15 - cc;
        if (cc > 14) cc = 14;
        lev[a] = cc + 1;
    }
    /* palette: 1 shadow, 2 + 2k / 3 + 2k ball edge / centre of shade k */
    for (a = 0; a < 15; a++)
        for (b = 2; b < 4; b++) {
            int cc = 100 + a * 9, n = 2 + 2 * a + (b - 2);
            pal[n * 3] = cols[b][0];
            pal[n * 3 + 1] = cols[b][1] * cc / 256;
            pal[n * 3 + 2] = cols[b][2] * cc / 256;
        }
    for (a = 0; a < 100; a++) {
        int cc = 64 - 256 / (a + 4);
        cc = cc * cc / 64;
        pal[(32 + a) * 3] = pal[(32 + a) * 3 + 1] = pal[(32 + a) * 3 + 2] = cc / 4;
    }
    pal[3] = pal[4] = pal[5] = pal[(32 + 23) * 3];     /* shadow: colour 87 */
    /* screens */
    for (i = 0; i < 3; i++) {
        buf[i] = k_alloc(BUFSZ);
        cop[i] = k_alloc((64 + 100 * 2) * 4);
        c = cop[i];
        *c++ = CMOVE(R_DIWSTRT, 0x2c81);
        *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
        *c++ = CMOVE(R_DDFSTRT, 0x0038);
        *c++ = CMOVE(R_DDFSTOP, 0x00d0);
        *c++ = CMOVE(R_BPL1MOD, ROW - 40);
        *c++ = CMOVE(R_BPL2MOD, ROW - 40);
        *c++ = CMOVE(R_BPLCON1, 0);
        *c++ = CMOVE(R_BPLCON2, 0);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        colp[i] = (u16 *)c;
        for (j = 0; j < 32; j++)
            *c++ = CMOVE(R_COLOR00 + j * 2, 0);
        for (j = 0; j < 5; j++) {
            u32 p = (u32)buf[i] + j * 42;
            *c++ = CMOVE(R_BPL1PTH + j * 4, p >> 16);
            *c++ = CMOVE(R_BPL1PTH + j * 4 + 2, p & 0xffff);
        }
        *c++ = CWAIT(LINE0, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x5200);
        floorp[i] = (u16 *)c;
        for (j = 0; j < 100; j++) {
            int l = LINE0 + 100 + j;
            /* line 256: the PAL wrap wait stands in for its WAIT */
            *c++ = l == 256 ? CWAIT_PAL : CWAIT(l & 0xff, 0x07);
            *c++ = CMOVE(R_COLOR00, 0);
        }
        /* (already past line 256: no PAL wrap wait here) */
        *c++ = CWAIT((LINE0 + 200) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CMOVE(R_COLOR00, 0);
        *c++ = CEND;
    }
    j = 0;
    for (a = 0; a < DOTN; a++) {
        dottaul[a] = a;
        dot[a][0] = 0;
        dot[a][1] = 2560 - dropper;
        dot[a][2] = 0;
        dot[a][3] = 0;
    }
    for (a = 0; a < 500; a++) {
        int x = rnd() % DOTN, y = rnd() % DOTN, d = dottaul[x];
        dottaul[x] = dottaul[y];
        dottaul[y] = d;
    }
    memset(pal2, 0, sizeof(pal2));
    setpal(pal2);
    k_set_copper(cop[0]);
    /* fade in (65 steps of 2 VGA frames) */
    t0 = k_frames;
    for (;;) {
        int s = 64 - (int)((k_frames - t0) * 7 / 5) / 2;
        if (s < 0) s = 0;
        for (a = 0; a < 132 * 3; a++)
            pal2[a] = pal[a] > s ? pal[a] - s : 0;
        setpal(pal2);
        k_waitvbl();
        if (!s || k_exit()) break;
    }
    t0 = k_frames;
    ph = 0;
    while (!k_exit() && frame < 2450) {
        u32 now = (k_frames - t0) * 7 / 5;
        a = k_mus_plus();
        if (a > -4 && a < 0) break;
        if (frame > 2300) setpal(pal2);
        while (ph < now) {
            ph++;
            frame++;
            if (frame == 500) f = 0;
            i = dottaul[j];
            if (++j >= DOTN) j = 0;
            if (frame < 500) {
                dot[i][0] = isin(f * 11) * 40;
                dot[i][1] = icos(f * 13) * 10 - dropper;
                dot[i][2] = isin(f * 17) * 40;
                dot[i][3] = 0;
            } else if (frame < 900) {
                dot[i][0] = icos(f * 15) * 55;
                dot[i][1] = dropper;
                dot[i][2] = isin(f * 15) * 55;
                dot[i][3] = -260;
            } else if (frame < 1700) {
                a = dsin[frame & 1023] / 8;
                dot[i][0] = icos(f * 66) * a;
                dot[i][1] = 8000;
                dot[i][2] = isin(f * 66) * a;
                dot[i][3] = -300;
            } else if (frame < 2360) {
                dot[i][0] = rnd() - 16384;
                dot[i][1] = 8000 - rnd() / 2;
                dot[i][2] = rnd() - 16384;
                dot[i][3] = 0;
                if (frame > 1900 && !(frame & 31) && grav > 0) grav--;
            } else if (frame < 2400) {
                a = frame - 2360;
                for (b = 0; b < 132 * 3; b += 3) {
                    int v;
                    v = pal[b] + a * 3; pal2[b] = v > 63 ? 63 : v;
                    v = pal[b + 1] + a * 3; pal2[b + 1] = v > 63 ? 63 : v;
                    v = pal[b + 2] + a * 4; pal2[b + 2] = v > 63 ? 63 : v;
                }
            } else if (frame < 2440) {
                a = 63 - (frame - 2400) * 2;
                if (a < 0) a = 0;
                memset(pal2, a, sizeof(pal2));
            }
            if (dropper > 4000) dropper -= 100;
            rotcos = icos(rot) * 64;
            rotsin = isin(rot) * 64;
            rots += 2;
            if (frame > 1900) {
                rot += rota / 64;
                rota--;
            } else
                rot = isin(rots);
            f++;
            gravity = grav;
            gravityd = gravd;
            /* gravity and bounces (drawdots in the original, once per frame) */
            for (b = 0; b < DOTN; b++) {
                s16 *d = dot[b];
                int y;
                d[3] += gravity;
                y = d[1] + d[3];
                if (y >= 8105) {
                    d[3] = (s16)(-d[3] * gravityd) >> 4;
                    y += d[3];
                }
                d[1] = y;
            }
        }
        dots_draw(&dot[0][0], DOTN, buf[cur], rotsin, rotcos, dinv, pat + 16, rowoff, lev);
        k_set_copper(cop[cur]);
        k_waitvbl();
        /* clear the screen shown before (drawn into again in two frames) */
        b = cur ? cur - 1 : 2;
        k_wait_blitter();
        BLTCON0 = 0x0100;
        BLTCON1 = 0;
        BLTDPT = (u32)buf[b];
        BLTDMOD = 0;
        BLTSIZE = ((BUFSZ / 84) << 6) | 42;
        cur = cur == 2 ? 0 : cur + 1;
    }
    k_wait_blitter();
}
