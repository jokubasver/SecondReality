/*
 * MNTSCRL - text scrolling over the mountain landscape (FOREST/READ2.PAS,
 * Trug).  The text lights up the landscape where it lies on it; three
 * mapping tables each refresh a third of the text pixels per frame.
 */
#include "kernel.h"
#include "mntscrl_data.h"

#define H 200
#define PL 8000
#define LINE0 0x48
#define VEKE 2800

void mnt_draw(const u16 *map, const u32 *newc, const u32 *oldc, u8 *plane);

static u8 *scr[2];                  /* one used */
static u32 *cop[2];
static u16 *colp[2];

static void set_pal(int bgl, int hll)    /* fade levels 0..64 */
{
    int i;
    for (i = 0; i < 32; i++) {
        const u8 *c = bg_pal + i * 3;
        int l = i < 16 ? bgl : hll;
        u16 v = (((c[0] * l >> 6) >> 2) << 8) | (((c[1] * l >> 6) >> 2) << 4) | ((c[2] * l >> 6) >> 2);
        colp[0][i * 2 + 1] = v;
        colp[1][i * 2 + 1] = v;
    }
}

void part_main(void)
{
    static const u16 *const maps[3] = { map0, map1, map2 };
    u32 *c, t0;
    int i, t, b, k, fp = 0, fadeout = 0;
    /* text position each table was last drawn at, per buffer (only the
     * changed pixels are drawn); far left = no text yet */
    int lastk[1][3] = { { -136, -136, -136 } };
    for (b = 0; b < 1; b++) {
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
        *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
    }
    colp[1] = colp[0];
    set_pal(0, 0);
    k_set_copper(cop[0]);
    /* landscape fades in */
    t0 = k_frames;
    while ((t = (k_frames - t0) * 7 / 5) < 64 && !k_exit()) {
        set_pal(t, t);
        k_waitvbl();
    }
    set_pal(64, 64);
    /* wait for the music */
    while (!k_exit() && k_mus_plus() < 0 && !k_mus_after(42, 0))
        k_waitvbl();
    /* scroll: phase = 70 Hz frame count, the text moves one pixel every
     * three phases.  As the original: one table per frame, the text
     * position kept for a round of three (the tables cover different
     * screen pixels); only the font pixels that changed since the table
     * was last drawn are written. */
    t0 = k_frames;
    k = 0;
    for (b = 0;; b = b == 2 ? 0 : b + 1) {
        int ph = (k_frames - t0) * 7 / 5;
        if (k_exit() || ph >= VEKE) break;
        if (k_mus_plus() == -11) fadeout = 1;
        if (fadeout) {
            fp += 2;
            if (fp >= 64) break;
            set_pal(64 - fp, 64 - fp);
        }
        if (b == 0)
            k = ph / 3;
        mnt_draw(maps[b], tcols + k + 136, tcols + lastk[0][b] + 136, scr[0] + 4 * PL);
        lastk[0][b] = k;
        k_waitvbl();
    }
    set_pal(0, 0);
    k_waitvbl();
}
