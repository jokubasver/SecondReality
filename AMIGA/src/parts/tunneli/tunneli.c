/*
 * TUNNELI - the dot tunnel (TUNNELI/TUN10.PAS, Trug).
 * 77 rings of 64 dots move towards the viewer along a wavy path; the
 * brightness falls off with depth and the rings alternate between two
 * brightness groups.
 */
#include "kernel.h"
#include "tunneli_data.h"

#define H 200
#define PL 8000
#define LINE0 0x48
#define VEKE 1060

void tun_ring(const s16 *pts, int dx, int dy40, u8 *plane, int mask);

struct ring { s16 x, y; u8 c; };
static struct ring putki[101];
static u8 *buf[3];                  /* triple: the blitter clears one */
static u32 *cop[3];
static int sade[101];
static s16 ext[101][4];             /* ring extents: x min/max, y*40 min/max */

static void build_copper(int b)
{
    u32 *c = cop[b];
    int i;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPL2MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    /* colour index: group bit 3, brightness 1..7 */
    for (i = 0; i < 16; i++) {
        int v = i & 7, l = v * 63 / 7, r;
        if (i & 8) l = l * 3 / 4;
        r = l >> 2;
        *c++ = CMOVE(R_COLOR00 + i * 2, (r << 8) | (r << 4) | r);
    }
    for (i = 0; i < 4; i++) {
        u32 a = (u32)buf[b] + i * PL;
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

void part_main(void)
{
    int x, cur = 0, frame = 0, sx = 0, sy = 0, quit = 0;
    u32 last;
    for (x = 0; x < 3; x++) {
        buf[x] = k_alloc(PL * 4);
        cop[x] = k_alloc(64 * 4);
        memset(buf[x], 0, PL * 4);
        build_copper(x);
    }
    for (x = 0; x <= 100; x++) {
        const s16 *p;
        int i;
        sade[x] = 16384 / (x * 7 + 95);
        p = pcalc + sade[x] * 128;
        ext[x][0] = ext[x][2] = 32767;
        ext[x][1] = ext[x][3] = -32768;
        for (i = 0; i < 64; i++, p += 2) {
            if (p[0] < ext[x][0]) ext[x][0] = p[0];
            if (p[0] > ext[x][1]) ext[x][1] = p[0];
            if (p[1] < ext[x][2]) ext[x][2] = p[1];
            if (p[1] > ext[x][3]) ext[x][3] = p[1];
        }
    }
    memset(putki, 0, sizeof(putki));
    k_set_copper(cop[0]);
    last = k_frames;
    while (!quit && !k_exit()) {
        int steps;
        u8 *b;
        /* draw into the buffer the blitter cleared meanwhile, start
         * clearing the one shown until now */
        cur = cur == 2 ? 0 : cur + 1;
        b = buf[cur];
        k_wait_blitter();
        for (x = 80; x >= 4; x--) {
            int bx = putki[x].x - putki[5].x;
            int by = putki[x].y - putki[5].y;
            int k = (x * 10 + 6) / 13;          /* round(x / 1.3) */
            int c = putki[x].c, idx;
            if (!c || k == 4)
                continue;
            /* brightness 64-k (0..63) to 1..7 */
            idx = ((64 - k) * 7 + 32) / 64;
            if (idx < 1) idx = 1;
            if (idx > 7) idx = 7;
            if (c == 128) idx |= 8;
            if (bx + ext[x][0] >= 0 && bx + ext[x][1] <= 319 &&
                by * 40 + ext[x][2] >= 0 && by * 40 + ext[x][3] <= 199 * 40)
                idx |= 16;                  /* on the screen: no clipping */
            tun_ring(pcalc + sade[x] * 128, bx, by * 40, b, idx);
        }
        k_set_copper(cop[cur]);
        k_waitvbl();
        k_wait_blitter();
        k_blit_clear(buf[cur ? cur - 1 : 2], 0, 20, 4 * H);   /* 4 planes = 800 rows */
        /* advance the tunnel one ring per 70 Hz frame */
        steps = (k_frames - last) * 7 / 5 - frame;     /* last = start */
        while (steps-- > 0) {
            putki[100].x = cosit[sy & 2047] - sinit[(sy * 3) & 4095] - cosit[sx & 2047];
            putki[100].y = sinit[(sx * 2) & 4095] - cosit[sx & 2047];
            memmove(&putki[0], &putki[1], sizeof(struct ring) * 100);
            sy++;
            sx++;
            putki[99].c = (sy & 15) > 7 ? 128 : 64;
            if (frame >= VEKE - 102) putki[99].c = 0;
            if (frame == VEKE) { quit = 1; break; }
            frame++;
        }
    }
    k_set_copper(k_empty_copper);
    k_waitvbl();
}
