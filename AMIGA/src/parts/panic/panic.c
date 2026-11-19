/*
 * PANICEND - the picture switches off like an old TV (PANIC/SHUTDOWN.C):
 * it collapses into a white horizontal line, the line shrinks to a dot,
 * the dot glows for a moment and the screen goes black.
 */
#include "kernel.h"

#define LINE0 0x48
#define H 200

static u32 *cop;
static u8 *line;                 /* one plane, one row */

static u16 col6(int r, int g, int b)
{
    return ((r >> 2) << 8) | ((g >> 2) << 4) | (b >> 2);
}

void part_main(void)
{
    const u8 *pic = (const u8 *)*(u32 *)k_msg[1];
    int bpr = *(u16 *)(k_msg[1] + 4);
    u8 pal[96];
    int a, x, i, y;
    u32 t;
    k_hold_old();
    memcpy(pal, k_msg[2], 64);
    memcpy(pal + 64, k_msg[3], 32);
    cop = k_alloc(4 * (64 + 6 * 260));
    line = k_alloc(48);

    /* collapse: 2a lines centred on row 100, fading towards white */
    for (a = 32; a > 2 && !k_exit(); a = a * 5 / 6) {
        u32 *c = cop;
        int f = 63 - a, top = 100 - a, h = 2 * a;
        *c++ = CMOVE(R_DIWSTRT, 0x2c81);
        *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
        *c++ = CMOVE(R_DDFSTRT, 0x0038);
        *c++ = CMOVE(R_DDFSTOP, 0x00d0);
        *c++ = CMOVE(R_BPLCON1, 0);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CMOVE(R_COLOR00, 0xfff);
        for (i = 1; i < 32; i++)
            *c++ = CMOVE(R_COLOR00 + i * 2, col6((f * 63 + pal[i * 3] * (64 - f)) >> 6,
                                                 (f * 63 + pal[i * 3 + 1] * (64 - f)) >> 6,
                                                 (f * 63 + pal[i * 3 + 2] * (64 - f)) >> 6));
        for (i = 0; i < 5; i++) {
            u32 p = (u32)pic + i * bpr * H;
            *c++ = CMOVE(R_BPL1PTH + i * 4, p >> 16);
            *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, p & 0xffff);
        }
        /* line y shows source row y*200/h; the modulo skips the rest */
        for (y = 0; y < h; y++) {
            int r0 = y * H / h, r1 = (y + 1) * H / h;
            int mod = (r1 - r0) * bpr - 40;
            int ln = LINE0 + top + y;
            if (ln == 256) *c++ = CWAIT_PAL;
            *c++ = CWAIT(ln & 0xff, 0x07);
            if (y == 0) *c++ = CMOVE(R_BPLCON0, 0x5200);
            *c++ = CMOVE(R_BPL1MOD, mod);
            *c++ = CMOVE(R_BPL2MOD, mod);
        }
        *c++ = CWAIT((LINE0 + top + h) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CMOVE(R_COLOR00, 0x000);
        *c++ = CEND;
        k_set_copper(cop);
        k_waitvbl();
        k_waitvbl();
    }
    k_release_old();

    /* white line shrinking to the centre */
    {
        u32 *c = cop;
        *c++ = CMOVE(R_DIWSTRT, 0x2c81);
        *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
        *c++ = CMOVE(R_DDFSTRT, 0x0038);
        *c++ = CMOVE(R_DDFSTOP, 0x00d0);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CMOVE(R_COLOR00, 0x000);
        *c++ = CMOVE(R_COLOR00 + 2, 0xfff);
        *c++ = CMOVE(R_BPL1PTH, (u32)line >> 16);
        *c++ = CMOVE(R_BPL1PTH + 2, (u32)line & 0xffff);
        *c++ = CMOVE(R_BPL1MOD, -40);
        *c++ = CWAIT(LINE0 + 99, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x1200);
        *c++ = CWAIT(LINE0 + 101, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
    }
    memset(line, 0xff, 40);
    k_set_copper(cop);
    t = k_frames;
    for (x = 20; x <= 160 && !k_exit(); x += 3) {
        for (i = 0; i < 320; i++)
            if (i < x || i > 320 - x)
                line[i >> 3] &= ~(0x80 >> (i & 7));
        k_waitline(0x20);
        while ((k_frames - t) * 7 / 5 < (x - 20) / 3 + 1)
            k_waitvbl();
    }
    memset(line, 0, 40);
    line[20] = 0x80;
    /* the dot glows */
    t = k_frames;
    for (a = 0; a < 60 && !k_exit();) {
        int b = muls16(k_sin(a * 1024 * 3 / 120 + 256), 31) / 16384 + 32;
        cop[6] = CMOVE(R_COLOR00 + 2, col6(b, b, b));
        k_waitvbl();
        a = (k_frames - t) * 7 / 5;
    }
    memset(line, 0, 40);
    for (i = 0; i < 50; i++)
        k_waitvbl();
    k_set_copper(k_empty_copper);
}
