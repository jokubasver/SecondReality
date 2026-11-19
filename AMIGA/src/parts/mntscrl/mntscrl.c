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

void mnt_draw(const u16 *map, const u32 *textcols, int k, u8 *plane);

static u8 *scr;
static u32 *cop;
static u16 *colp;

static void set_pal(int bgl, int hll)    /* fade levels 0..64 */
{
    int i;
    for (i = 0; i < 32; i++) {
        const u8 *c = bg_pal + i * 3;
        int l = i < 16 ? bgl : hll;
        colp[i * 2 + 1] = (((c[0] * l >> 6) >> 2) << 8) | (((c[1] * l >> 6) >> 2) << 4) | ((c[2] * l >> 6) >> 2);
    }
}

void part_main(void)
{
    static const u16 *const maps[3] = { map0, map1, map2 };
    u32 *c, t0;
    int i, t, fp = 0, fadeout = 0;
    scr = k_alloc(PL * 5);
    memcpy(scr, bg, PL * 4);
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
        *c++ = CMOVE(R_COLOR00 + i * 2, 0);
    for (i = 0; i < 5; i++) {
        u32 a = (u32)scr + i * PL;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x5200);
    *c++ = CWAIT_PAL;
    *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
    set_pal(0, 0);
    k_set_copper(cop);
    /* landscape fades in */
    t0 = k_frames;
    while ((t = (k_frames - t0) * 7 / 5) < 64 && !k_exit()) {
        set_pal(t, t);
        k_waitvbl();
    }
    set_pal(64, 64);
    /* wait for the music */
    while (!k_exit() && k_mus_plus() < 0)
        k_waitvbl();
    /* scroll: phase = 70 Hz frame count, one table per phase, the text
     * moves one pixel every three phases */
    t0 = k_frames;
    for (;;) {
        int ph = (k_frames - t0) * 7 / 5;
        if (k_exit() || ph >= VEKE) break;
        if (k_mus_plus() == -11) fadeout = 1;
        if (fadeout) {
            fp += 2;
            if (fp >= 64) break;
            set_pal(64 - fp, 64 - fp);
        }
        mnt_draw(maps[ph % 3], textcols, ph / 3, scr + 4 * PL);
        k_waitvbl();
    }
    set_pal(0, 0);
    k_waitvbl();
}
