/*
 * ENDLOGO - the Future Crew logo fades in from white, stays until the
 * music's next mark and fades to black (END/END.C).
 */
#include "kernel.h"
#include "endlogo_data.h"

#define PL 8000
#define LINE0 0x48

static u16 *colp;

static void set_pal(int c, int black)  /* c: 0 white .. 128 picture */
{
    int i, k;
    for (i = 0; i < 32; i++) {
        int v[3];
        for (k = 0; k < 3; k++) {
            int p = epal[i * 3 + k];
            v[k] = black ? p * c / 64 : ((128 - c) * 63 + p * c) / 128;
        }
        colp[i * 2 + 1] = ((v[0] >> 2) << 8) | ((v[1] >> 2) << 4) | (v[2] >> 2);
    }
}

void part_main(void)
{
    u32 *cop, *c, t0;
    int i, f;
    cop = k_alloc(64 * 4);
    c = cop;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPL2MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    colp = (u16 *)c;
    for (i = 0; i < 32; i++)
        *c++ = CMOVE(R_COLOR00 + i * 2, 0xfff);
    for (i = 0; i < 5; i++) {
        u32 a = (u32)epic + i * PL;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x5200);
    *c++ = CWAIT_PAL;
    *c++ = CWAIT((LINE0 + 200) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CMOVE(R_COLOR00, 0xfff);
    *c++ = CEND;
    set_pal(0, 0);
    k_set_copper(cop);
    /* 32 frames white, fade in over 128 */
    t0 = k_frames;
    while (!k_exit() && (f = (k_frames - t0) * 7 / 5) < 32 + 128) {
        if (f >= 32) set_pal(f - 32, 0);
        k_waitvbl();
    }
    set_pal(128, 0);
    cop[c - cop - 2] = CMOVE(R_COLOR00, 0);
    t0 = k_frames;
    while (!k_exit() && (k_frames - t0) * 7 / 5 < 5000 && k_mus_plus() <= -16)
        k_waitvbl();
    t0 = k_frames;
    while (!k_exit() && (f = (k_frames - t0) * 7 / 5) < 64) {
        set_pal(63 - f, 1);
        k_waitvbl();
    }
    set_pal(0, 1);
    k_waitvbl();
}
