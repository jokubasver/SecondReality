#include "kernel_blit.h"
u16 BLTCON0, BLTCON1, BLTAFWM = 0xffff, BLTALWM = 0xffff, BLTAMOD, BLTBMOD, BLTCMOD, BLTDMOD, BLTSIZE, DMACONR;
u32 BLTAPT, BLTBPT, BLTCPT, BLTDPT;
static u16 last_size;
static u16 rd(u32 a) { u8 *p = (u8 *)(uintptr_t)a; return (p[0] << 8) | p[1]; }
static void wr(u32 a, u16 v) { u8 *p = (u8 *)(uintptr_t)a; p[0] = v >> 8; p[1] = v; }
void k_wait_blitter(void)
{
    int h, w, y, x, desc, step, ife, efe;
    u16 mt;
    if (!BLTSIZE) return;
    h = BLTSIZE >> 6; if (!h) h = 1024;
    w = BLTSIZE & 63; if (!w) w = 64;
    desc = BLTCON1 & 2; ife = BLTCON1 & 8; efe = BLTCON1 & 16;
    step = desc ? -2 : 2;
    mt = BLTCON0 & 0xff;
    for (y = 0; y < h; y++) {
        int fc = (BLTCON1 >> 2) & 1;
        for (x = 0; x < w; x++) {
            u16 a = (BLTCON0 & 0x800) ? rd(BLTAPT) : 0, b = 0, c = (BLTCON0 & 0x200) ? rd(BLTCPT) : 0, d = 0;
            int bit;
            if (x == 0) a &= BLTAFWM;
            if (x == w - 1) a &= BLTALWM;
            for (bit = 0; bit < 16; bit++) {
                int ab = (a >> bit) & 1, bb = (b >> bit) & 1, cb = (c >> bit) & 1;
                int idx = (ab << 2) | (bb << 1) | cb;
                if ((mt >> idx) & 1) d |= 1 << bit;
            }
            if (ife || efe) {
                u16 o = 0;
                for (bit = 0; bit < 16; bit++) {
                    int in = (d >> bit) & 1;
                    if (in) fc ^= 1;
                    if (efe ? fc : (fc | in)) o |= 1 << bit;
                }
                d = o;
            }
            if (BLTCON0 & 0x100) wr(BLTDPT, d);
            if (BLTCON0 & 0x800) BLTAPT += step;
            if (BLTCON0 & 0x200) BLTCPT += step;
            BLTDPT += step;
        }
        if (BLTCON0 & 0x800) BLTAPT += desc ? -BLTAMOD : (s16)BLTAMOD;
        if (BLTCON0 & 0x200) BLTCPT += desc ? -BLTCMOD : (s16)BLTCMOD;
        BLTDPT += desc ? -(s16)BLTDMOD : (s16)BLTDMOD;
    }
    BLTSIZE = 0;
}
