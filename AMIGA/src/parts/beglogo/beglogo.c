/*
 * BEGLOGO - the "Second Reality" title picture (BEG/BEG.C).
 * The screen goes white, then the picture fades in from white.
 */
#include "kernel.h"
#include "beglogo_data.h"

#define H 200
#define PL (40 * H)
#define LINE0 0x48

static u32 *cop;
static u16 *colp;              /* colour moves inside the copper list */

static const u8 white[96] = {
    63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
    63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
    63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
    63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63 };

static void set_level(int c)    /* 0 = white .. 128 = picture */
{
    u16 col[32];
    int i;
    k_fade_pal(white, pic_pal, col, 32, c >> 1);
    for (i = 0; i < 32; i++)
        colp[i * 2 + 1] = col[i];
}

void part_main(void)
{
    u32 *c, t0;
    int i, t;
    cop = k_alloc(80 * 4);
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
        u32 a = (u32)pic + i * PL;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x5200);
    *c++ = CWAIT_PAL;
    *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CMOVE(R_COLOR00, 0xfff);
    *c++ = CEND;
    set_level(0);
    k_set_copper(cop);
    /* white for 32 VGA frames, then fade over 128 */
    t0 = k_frames;
    while ((t = (k_frames - t0) * 7 / 5) < 32 + 128 && !k_exit()) {
        if (t >= 32)
            set_level(t - 32);
        k_waitvbl();
    }
    set_level(128);
    /* GLENZ continues from this picture: planes and final colours */
    {
        u32 *m = (u32 *)k_msg[1];
        m[0] = (u32)pic;
        for (i = 0; i < 32; i++)
            ((u16 *)(k_msg[1] + 4))[i] = colp[i * 2 + 1];
    }
    /* the border goes from white to the picture's background colour */
    cop[0] = cop[0];
    {
        u32 *e = c - 2;
        *e = CMOVE(R_COLOR00, ((u16 *)colp)[1]);
    }
    k_waitvbl();
}
