/*
 * RAYSCRL - a sword scrolls through the reflections of a raytraced water
 * scene (WATER/DEMO.PAS, ROUTINES.ASM).
 *
 * Three precalculated maps say where each pixel of the 158x34 scroll
 * buffer shows on the screen; one map is redrawn per frame.  Amiga
 * version: the background in 16 colours, the sword as plane 5 that tints
 * it (colours 16-31), and only the pixels whose sword bit changed since
 * that map was last drawn are written.
 */
#include "kernel.h"
#include "water_data.h"

#define PL 8000
#define LINE0 0x48

void ray_draw(const u16 *map, const u8 *newm, const u8 *oldm, u8 *screen);

static u8 *scr[2];                  /* double buffered */
static u32 *cop[2];
static u16 *colp[2];
static u8 full[96];

/* level 0..64 of the final palette */
static void set_pal(int l)
{
    int i;
    for (i = 0; i < 32; i++) {
        const u8 *c = full + i * 3;
        u16 v = (((c[0] * l >> 6) >> 2) << 8) | (((c[1] * l >> 6) >> 2) << 4) | ((c[2] * l >> 6) >> 2);
        colp[0][i * 2 + 1] = v;
        colp[1][i * 2 + 1] = v;
    }
}

void part_main(void)
{
    static const u16 *const maps[3] = { wmap1, wmap2, wmap3 };
    int last[2][3] = { { -1, -1, -1 }, { -1, -1, -1 } };
    u32 *c, t0;
    int i, m, b, co, f, fp = 0, fadeout = -1, scp = 0;
    /* background colours 0-15, the sword's own colours 16-31 */
    for (i = 0; i < 48; i++) {
        full[i] = bgpal[i];
        full[48 + i] = swpal[i];
    }
    for (b = 0; b < 2; b++) {
        scr[b] = k_alloc(PL * 5);
        memcpy(scr[b], bg, PL * 4);
        cop[b] = k_alloc(64 * 4);
        c = cop[b];
        *c++ = CMOVE(R_DIWSTRT, 0x2c81);
        *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
        *c++ = CMOVE(R_DDFSTRT, 0x0038);
        *c++ = CMOVE(R_DDFSTOP, 0x00d0);
        *c++ = CMOVE(R_BPL1MOD, 0);
        *c++ = CMOVE(R_BPL2MOD, 0);
        *c++ = CMOVE(R_BPLCON1, 0);
        *c++ = CMOVE(R_BPLCON2, 0);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        colp[b] = (u16 *)c;
        for (i = 0; i < 32; i++)
            *c++ = CMOVE(R_COLOR00 + i * 2, 0);
        for (i = 0; i < 5; i++) {
            u32 a = (u32)scr[b] + i * PL;
            *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
            *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
        }
        *c++ = CWAIT(LINE0, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x5200);
        *c++ = CWAIT_PAL;
        *c++ = CWAIT((LINE0 + 200) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
    }
    set_pal(0);
    k_set_copper(cop[0]);
    while (!k_exit() && k_mus_plus() < 0 && !k_mus_after(88, 0))
        k_waitvbl();
    co = k_mus_order;
    /* fade in (one step every 2 VGA frames) */
    t0 = k_frames;
    while (!k_exit() && (f = (k_frames - t0) * 7 / 5) < 128) {
        set_pal(f / 2);
        k_waitvbl();
    }
    set_pal(64);
    /* wait for the next pattern, row 16 */
    while (!k_exit() && (k_mus_order == co || k_mus_row < 16))
        k_waitvbl();
    /* scroll: the sword moves a pixel every 3 VGA frames */
    t0 = k_frames;
    b = 1;
    for (;;) {
        f = (k_frames - t0) * 7 / 5;
        if (k_exit()) break;
        if (fadeout < 0 && k_mus_plus() == -11) fadeout = f;
        if (fadeout >= 0) {
            fp = (f - fadeout) + 1;
            if (fp >= 64) break;
            set_pal(64 - fp);
        }
        /* the three maps cover different screen pixels: all three are
         * drawn at the same scroll position into the hidden buffer, then
         * it is shown (drawn one per frame at moving positions they
         * interleave into stripes) */
        scp = f / 3;
        if (scp > FW - 159) scp = FW - 159;
        for (m = 0; m < 3; m++) {
            ray_draw(maps[m], swmask + scp + 1, swmask + last[b][m] + 1, scr[b]);
            last[b][m] = scp;
        }
        k_set_copper(cop[b]);
        b ^= 1;
        k_waitvbl();
    }
    set_pal(0);
    k_waitvbl();
}
