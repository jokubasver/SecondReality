/*
 * ENDSCRL - the end scroller (ENDSCRL/MAIN.C): 640 pixel wide text in
 * three greys moving up one line every two VGA frames.  Amiga version:
 * hires, 2 bitplanes; like the original each new pixel row is written
 * twice into a doubled circular buffer, so the display start is just a
 * pointer.
 */
#include "kernel.h"
#include "endscrl_data.h"

#define LINE0 0x2c
#define H 256                   /* lines shown */
#define R (H + 1)               /* circular buffer rows */
#define PL (80 * R * 2)

static u8 *buf;
static u32 *ptrs;

void part_main(void)
{
    static u8 scan[2][80];
    static u8 tline[100];
    u32 *cop, *c, t0, ph = 0;
    const u8 *tp = etext;
    int i, yscrl = 0, line = 0, chars = 0, tstart = 0, endrows = 0;
    buf = k_alloc(PL * 2);
    cop = k_alloc(32 * 4);
    c = cop;
    *c++ = CMOVE(R_DIWSTRT, (LINE0 << 8) | 0x81);
    *c++ = CMOVE(R_DIWSTOP, (((LINE0 + H) & 0xff) << 8) | 0xc1);
    *c++ = CMOVE(R_DDFSTRT, 0x003c);
    *c++ = CMOVE(R_DDFSTOP, 0x00d4);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPL2MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPLCON0, 0xa200);
    *c++ = CMOVE(R_COLOR00, 0x000);
    *c++ = CMOVE(R_COLOR00 + 2, 0x555);
    *c++ = CMOVE(R_COLOR00 + 4, 0xaaa);
    *c++ = CMOVE(R_COLOR00 + 6, 0xfff);
    ptrs = c;
    for (i = 0; i < 2; i++) {
        u32 a = (u32)buf + i * PL;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CEND;
    k_set_copper(cop);
    t0 = k_frames;
    while (!k_exit() && endrows <= R + FONAY) {
        u32 now = (k_frames - t0) * 7 / 10;     /* one pixel row per 2 VGA frames */
        while (ph < now) {
            int a, x, b;
            ph++;
            /* text done: stop once the last line has scrolled out */
            if (endrows || !*tp)
                endrows++;
            if (line == 0) {
                /* next text line, centred in 640 pixels */
                chars = 0;
                tstart = 0;
                while (*tp && *tp != '\n' && chars < 99) {
                    tline[chars++] = *tp;
                    tstart += efontab[*tp * 2 + 1] + 2;
                    tp++;
                }
                if (*tp == '\n') tp++;
                tstart = (639 - tstart) / 2;
            }
            memset(scan, 0, sizeof(scan));
            for (a = 0, x = tstart; a < chars; a++, x += 2) {
                int sx = efontab[tline[a] * 2], w = efontab[tline[a] * 2 + 1];
                const u8 *f = efont + line * 1500 + sx;
                for (b = 0; b < w; b++, x++) {
                    int v = f[b];
                    if (v && x >= 0 && x < 640) {
                        u8 m = 0x80 >> (x & 7);
                        if (v & 1) scan[0][x >> 3] ^= m;
                        if (v & 2) scan[1][x >> 3] ^= m;
                    }
                }
            }
            for (i = 0; i < 2; i++) {
                memcpy(buf + i * PL + yscrl * 80, scan[i], 80);
                memcpy(buf + i * PL + (yscrl + R) * 80, scan[i], 80);
            }
            yscrl = yscrl + 1 == R ? 0 : yscrl + 1;
            line = line + 1 == FONAY ? 0 : line + 1;
        }
        for (i = 0; i < 2; i++) {
            u32 a = (u32)buf + i * PL + (yscrl + 1) * 80;
            ptrs[i * 2] = CMOVE(R_BPL1PTH + i * 4, a >> 16);
            ptrs[i * 2 + 1] = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
        }
        k_waitvbl();
    }
}
