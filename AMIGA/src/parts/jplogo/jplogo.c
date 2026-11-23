/*
 * JPLOGO - the picture slides in, then bounces like jelly (JPLOGO/JP.C).
 *
 * Amiga version: for the bounce the picture is stored at 13 widths, 8
 * pixels apart (100 rows each), at rest at its own width with all 200
 * rows; per display line the copper sets the bitplane modulos, which jump
 * to any row of any width.  So the vertical squash and the horizontal
 * bulge cost 200 modulo values per frame.
 */
#include "kernel.h"
#include "jplogo_data.h"

#define LINE0 0x48
#define PSTRIDE (NV * 32)       /* plane stride inside a row group */

static u32 *cop[2];
static u32 *ptrs[2];
static u16 *mods[2];
static s16 framey1[201], framey2[201];
/* row tables (no divisions per line): offsets of the full picture rows
 * and of the bounce rows, and bq * 32 / 25 */
static u32 fullrow[200], bncrow[200];
static s16 bq25[400];

static inline s16 divs16(s32 n, s16 d)
{
#ifdef __m68k__
    __asm__("divs.w %2,%0" : "=d"(n) : "0"(n), "d"(d) : "cc");
    return (s16)n;
#else
    return n / d;
#endif
}

#define FULL 255                /* version: the 184 pixel picture, 200 rows */

/* g: picture row (0..199, -1/200 = empty), v: width version or FULL */
static const u8 *rowptr(int g, int v)
{
    /* empty rows: a zero group next to the table in use (the modulo is
     * 16 bit: a line can only jump +-32 KB to the next) */
    if (g < 0)
        return jpic;
    if (g >= 200)
        return jpic + (v == FULL ? EGROUP : BEGROUP) * GROUP;
    if (v == FULL)
        return jpic + fullrow[g];
    return jpic + bncrow[g] + v * 32;
}

/* build frame data: rows[L] = row, vers[L] = width version (row -1 = empty) */
static void make(int b, const s16 *rows, const u8 *vers)
{
    const u8 *cur, *nxt;
    int l, p;
    cur = rowptr(rows[0], vers[0]);
    for (p = 0; p < 5; p++) {
        u32 a = (u32)(cur + p * PSTRIDE);
        ptrs[b][p * 2] = CMOVE(R_BPL1PTH + p * 4, a >> 16);
        ptrs[b][p * 2 + 1] = CMOVE(R_BPL1PTH + p * 4 + 2, a & 0xffff);
    }
    for (l = 0; l < 200; l++) {
        s32 m;
        nxt = l < 199 ? rowptr(rows[l + 1], vers[l + 1]) : cur + 32;
        m = (s32)(nxt - (cur + 32));
        mods[b][l * 6 + 3] = (u16)m;
        mods[b][l * 6 + 5] = (u16)m;
        cur = nxt;
    }
}

static void calc_frames(void)
{
    int frame, halt = 0, a = 0, la, mika = 1;
    int y1 = 0, y1a = 500, y2 = 399 * 16, y2a = 500;
    for (frame = 0; frame < 200; frame++) {
        if (!halt) {
            y1 += y1a;
            y2 += y2a;
            y2a += 16;
            if (y2 > 400 * 16) {
                y2 -= y2a;
                y2a = -y2a * mika / 8;
                if (mika < 4) mika += 3;
            }
            y1a += 16;
            la = a;
            a = (y2 - y1) - 400 * 16;
            if ((a & 0x8000) ^ (la & 0x8000))
                y1a = y1a * 7 / 8;
            y1a += a / 8;
            y2a -= a / 8;
        }
        if (frame > 90) {
            if (y2 >= 399 * 16) {
                y2 = 400 * 16;
                halt = 1;
            } else
                y2a = 8;
            y1 = y2 - 400 * 16;
        }
        framey1[frame] = y1;
        framey2[frame] = y2;
    }
    framey1[200] = framey1[199];
    framey2[200] = framey2[199];
}

void part_main(void)
{
    static s16 rows[200];
    static u8 vers[200];
    u32 *c, t0;
    int i, b = 0, l, frame;
    calc_frames();
    for (i = 0; i < 200; i++) {
        fullrow[i] = (FGROUP0 + i / NV) * GROUP + (i % NV) * 32;
        bncrow[i] = (1 + i * BROWS / 200) * GROUP;
    }
    for (i = 0; i < 400; i++)
        bq25[i] = i * 32 / 25;
    for (i = 0; i < 2; i++) {
        cop[i] = k_alloc((64 + 200 * 3) * 4);
        c = cop[i];
        /* 256 pixels wide, lines LINE0..LINE0+199 (the bitplane DMA
         * starts with the window, using the pointers set above it) */
        *c++ = CMOVE(R_DIWSTRT, (LINE0 << 8) | 0xa1);
        *c++ = CMOVE(R_DIWSTOP, (((LINE0 + 200) & 0xff) << 8) | 0xa1);
        *c++ = CMOVE(R_DDFSTRT, 0x0048);
        *c++ = CMOVE(R_DDFSTOP, 0x00c0);
        *c++ = CMOVE(R_BPLCON1, 0);
        *c++ = CMOVE(R_BPLCON2, 0);
        *c++ = CMOVE(R_BPLCON0, 0x5200);
        for (l = 0; l < 32; l++)
            *c++ = CMOVE(R_COLOR00 + l * 2, (((jpal[l * 3] >> 2) << 8) | ((jpal[l * 3 + 1] >> 2) << 4) | (jpal[l * 3 + 2] >> 2)));
        ptrs[i] = c;
        for (l = 0; l < 10; l++)
            *c++ = CMOVE(0x1fe, 0);
        mods[i] = (u16 *)c;
        for (l = 0; l < 200; l++) {
            if (LINE0 + l == 256) {
                /* the PAL wrap wait replaces this line's WAIT */
                *c++ = CWAIT_PAL;
            } else
                *c++ = CWAIT((LINE0 + l) & 0xff, 0x07);
            *c++ = CMOVE(R_BPL1MOD, 0);
            *c++ = CMOVE(R_BPL2MOD, 0);
        }
        *c++ = CWAIT((LINE0 + 200) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
    }
    /* slide in from the top: memory row y/64 (400 line units) at line 0 */
    {
        int y = 400 * 64, a = 64;
        u32 ph = 0, now;
        t0 = k_frames;
        while (y > 0 && !k_exit()) {
            now = (k_frames - t0) * 7 / 5;
            while (ph < now && y > 0) {
                ph++;
                y -= a;
                a += 6;
                if (y < 0) y = 0;
            }
            for (l = 0; l < 200; l++) {
                int r = l + y / 128;
                rows[l] = r < 200 ? r : 200;
                vers[l] = FULL;
            }
            make(b, rows, vers);
            k_set_copper(cop[b]);
            k_waitvbl();
            b ^= 1;
        }
    }
    while (!k_exit() && k_mus_plus() < 4 && !k_mus_after(98, 4))
        k_waitvbl();
    /* the bounce */
    t0 = k_frames;
    for (;;) {
        int y1, y2, xsc, f4;
        frame = (k_frames - t0) * 7 / 5;
        if (frame >= 700 || k_exit()) break;
        f4 = frame > 511 ? 511 : frame;
        {
            int bb = f4 / 4, cc = f4 & 3, dd = 3 - cc;
            y1 = ((framey1[bb] * dd + framey1[bb + 1] * cc) / 3) / 16;
            y2 = ((framey2[bb] * dd + framey2[bb + 1] * cc) / 3) / 16;
        }
        xsc = (400 - (y2 - y1)) / 8;
        for (l = 0; l < 200; l++) {
            int y = l * 2;
            if (y < y1 || y >= y2 || y2 <= y1) {
                rows[l] = y < y1 ? -1 : 200;
                vers[l] = 0;
            } else {
                int bq = divs16((s32)(y - y1) * 400, y2 - y1);
                int a = 184 + divs16(jsin[bq25[bq] & 1023] * xsc + 32, 64);
                int v = (a - 136 + 4) >> 3;
                if (v < 0) v = 0;
                if (v > NV - 1) v = NV - 1;
                rows[l] = bq >> 1;
                vers[l] = xsc || y1 ? v : FULL;   /* at rest: full resolution */
            }
        }
        make(b, rows, vers);
        k_set_copper(cop[b]);
        k_waitvbl();
        b ^= 1;
    }
    /* fade to white (U2E.C does this after the music change) */
    t0 = k_frames;
    for (;;) {
        int f = (k_frames - t0) * 7 / 5, k;
        if (f > 33 || k_exit()) break;
        for (l = 0; l < 32; l++)
            for (k = 0; k < 2; k++) {
                int c[3], j;
                for (j = 0; j < 3; j++) {
                    c[j] = jpal[l * 3 + j] + f * 2;
                    if (c[j] > 63) c[j] = 63;
                }
                cop[k][7 + l] = CMOVE(R_COLOR00 + l * 2, ((c[0] >> 2) << 8) | ((c[1] >> 2) << 4) | (c[2] >> 2));
            }
        k_waitvbl();
    }
}
