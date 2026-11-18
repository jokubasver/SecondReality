/* Shared graphics helpers: blitter, palettes, music sync table. */
#include "kernel_int.h"

/* Blitter copy of a rectangle (A->D) with an optional right shift 0..15.
 * w = width in words (destination), mods in bytes.  A gets one extra word
 * masked off when shifting so callers pass w including the spill word. */
void k_blit_copy(const void *src, int smod, void *dst, int dmod, int w, int h, int shift)
{
    k_wait_blitter();
    BLTCON0 = 0x09f0 | (shift << 12);
    BLTCON1 = 0;
    BLTAFWM = 0xffff;
    BLTALWM = shift ? 0x0000 : 0xffff;
    BLTAPT = (u32)src;
    BLTDPT = (u32)dst;
    BLTAMOD = smod;
    BLTDMOD = dmod;
    BLTSIZE = (h << 6) | (w & 63);
}

/* OR-copy: D = A | C (C = destination) */
void k_blit_or(const void *src, int smod, void *dst, int dmod, int w, int h, int shift)
{
    k_wait_blitter();
    BLTCON0 = 0x0bfa | (shift << 12);
    BLTCON1 = 0;
    BLTAFWM = 0xffff;
    BLTALWM = shift ? 0x0000 : 0xffff;
    BLTAPT = (u32)src;
    BLTCPT = (u32)dst;
    BLTDPT = (u32)dst;
    BLTAMOD = smod;
    BLTCMOD = dmod;
    BLTDMOD = dmod;
    BLTSIZE = (h << 6) | (w & 63);
}

void k_blit_clear(void *dst, int dmod, int w, int h)
{
    k_wait_blitter();
    BLTCON0 = 0x0100;
    BLTCON1 = 0;
    BLTDPT = (u32)dst;
    BLTDMOD = dmod;
    BLTSIZE = (h << 6) | (w & 63);
}

/* Palettes are stored as 6 bit VGA style RGB triplets (0..63). */
u16 k_rgb(const u8 *c) { return ((c[0] >> 2) << 8) | ((c[1] >> 2) << 4) | (c[2] >> 2); }

/* out[i] = mix of a and b, level 0 (a) .. 64 (b), as Amiga colours */
void k_fade_pal(const u8 *a, const u8 *b, u16 *out, int n, int level)
{
    int i;
    if (level < 0) level = 0;
    if (level > 64) level = 64;
    for (i = 0; i < n; i++) {
        int r = (a[0] * (64 - level) + b[0] * level) >> 6;
        int g = (a[1] * (64 - level) + b[1] * level) >> 6;
        int bl = (a[2] * (64 - level) + b[2] * level) >> 6;
        out[i] = ((r >> 2) << 8) | ((g >> 2) << 4) | (bl >> 2);
        a += 3;
        b += 3;
    }
}

/* dis_sync(): value from the order/row table of the original DIS */
static const u16 synctab[][2] = {
    {0x0000, 0}, {0x0200, 1}, {0x0300, 2}, {0x032f, 3}, {0x042f, 4}, {0x052f, 5},
    {0x062f, 6}, {0x072f, 7}, {0x082f, 8}, {0x0900, 9}, {0x0d00, 10},
    {0x3d00, 1}, {0x3f00, 2}, {0x4100, 3}, {0x4200, 4}, {0xffff, 4},
};

int k_mus_sync(void)
{
    u16 pos = (k_mus_order << 8) | (k_mus_row & 0xff);
    int i;
    if (pos == 0) return 0;
    for (i = 1; i < 16; i++)
        if (pos <= synctab[i][0])
            return synctab[i - 1][1];
    return 4;
}

/* Free running E-clock counter (CIA-A timer B, 709 kHz, counts down). */
void k_eclock_init(void)
{
    CIAA_CRB = 0x00;
    CIAA_TBLO = 0xff;
    CIAA_TBHI = 0xff;
    CIAA_CRB = 0x11;               /* continuous, force load, start */
}

u16 k_eclock(void)
{
    u8 hi, lo, hi2;
    do {
        hi = CIAA_TBHI;
        lo = CIAA_TBLO;
        hi2 = CIAA_TBHI;
    } while (hi != hi2);
    return (u16)~((hi << 8) | lo);     /* counts up */
}
