/*
 * PAM - the planet explosion (original PAM/OUTTAA.C, Wildfire).
 * 40 animation frames (from PRAX4.FLI) over the horizon, with white
 * flashes done by fading the palette towards white.
 *
 * Each frame is a band of rows in 4 bitplanes (16 explosion colours, 0 =
 * see-through).  Frames are unpacked progressively while playing and
 * composited over the background with the blitter; plane 5 is the mask.
 */
#include "kernel.h"
#include "pam_data.h"

#define H 200
#define PL (40 * H)
#define LINE0 0x48

static u8 *buf[2];
static u8 *bands;
static u32 *cop[2];
static u16 colors[32];
static volatile int fade_level = 63;
static int prev_r0[2], prev_n[2];

static const int wfade[46] = {
    63, 63, 32, 16, 8, 4, 2, 1, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 2, 4, 6, 9, 14, 20, 28, 37, 46,
    56, 63, 63, 63, 63, 63
};

static void vbl(void)
{
    int i;
    static const u8 white[96] = {
        63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
        63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
        63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,
        63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63,63 };
    k_fade_pal(pam_pal, white, colors, 32, fade_level);
    for (i = 0; i < 32; i++)
        COLOR(i) = colors[i];
}

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
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    for (i = 0; i < 5; i++) {
        u32 a = (u32)buf[b] + i * PL;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x5200);
    *c++ = CWAIT(0xff, 0xde);              /* past line 255 */
    *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

static const u8 *const bgp[4] = { pam_bg0, pam_bg1, pam_bg2, pam_bg3 };

static void restore_rows(int b, int r0, int n)
{
    int p;
    if (n <= 0) return;
    for (p = 0; p < 4; p++)
        k_blit_copy(bgp[p] + r0 * 40, 0, buf[b] + p * PL + r0 * 40, 0, 20, n, 0);
    k_blit_clear(buf[b] + 4 * PL + r0 * 40, 0, 20, n);
}

static void compose(int b, int k)
{
    const u16 *ix = pam_index + k * 6;
    u32 off = ((u32)ix[2] << 16) | ix[3];
    int r0 = ix[4], n = ix[5], p;
    const u8 *band = bands + off;
    u8 *mask = buf[b] + 4 * PL + r0 * 40;
    restore_rows(b, prev_r0[b], prev_n[b]);
    prev_r0[b] = r0;
    prev_n[b] = n;
    if (n <= 0) return;
    /* mask = p0 | p1 | p2 | p3 */
    k_wait_blitter();
    BLTCON0 = 0x0ffe;                  /* A|B|C -> D */
    BLTCON1 = 0;
    BLTAFWM = BLTALWM = 0xffff;
    BLTAPT = (u32)band;
    BLTBPT = (u32)(band + n * 40);
    BLTCPT = (u32)(band + 2 * n * 40);
    BLTDPT = (u32)mask;
    BLTAMOD = BLTBMOD = BLTCMOD = BLTDMOD = 0;
    BLTSIZE = (n << 6) | 20;
    k_blit_or(band + 3 * n * 40, 0, mask, 0, 20, n, 0);
    /* planes: explosion | (background & ~mask) */
    for (p = 0; p < 4; p++) {
        u8 *d = buf[b] + p * PL + r0 * 40;
        k_wait_blitter();
        BLTCON0 = 0x0ff4;
        BLTCON1 = 0;
        BLTAPT = (u32)(band + p * n * 40);
        BLTBPT = (u32)(bgp[p] + r0 * 40);
        BLTCPT = (u32)mask;
        BLTDPT = (u32)d;
        BLTAMOD = BLTBMOD = BLTCMOD = BLTDMOD = 0;
        BLTSIZE = (n << 6) | 20;
    }
    k_wait_blitter();
}

void part_main(void)
{
    int f = 0, decoded = 0, shown = 0, cur = 0, b;
    u32 t0;
    buf[0] = k_alloc(PL * 5);
    buf[1] = k_alloc(PL * 5);
    cop[0] = k_alloc(40 * 4);
    cop[1] = k_alloc(40 * 4);
    for (b = 0; b < 2; b++) {
        restore_rows(b, 0, H);
        build_copper(b);
    }
    k_wait_blitter();
    fade_level = 0;
    k_vbl_hook = vbl;
    k_set_copper(cop[0]);
    bands = k_alloc(PAM_BANDS_SIZE + 64);

    /* original: wait for dis_sync() >= 10, then a new step every 4 VGA frames */
    while (k_mus_sync() < 10 && !k_exit()) {
        if (decoded < 40) {
            const u16 *ix = pam_index + decoded * 6;
            if (ix[5])
                k_unpack(pam_packed + (((u32)ix[0] << 16) | ix[1]), bands + (((u32)ix[2] << 16) | ix[3]));
            decoded++;
        } else
            k_waitvbl();
    }
    t0 = k_frames;
    while (!k_exit() && f < 45) {
        int step = (k_frames - t0) * 70 / (50 * 4) + 1;
        if (step > f) {
            f = step > 45 ? 45 : step;
            fade_level = wfade[f];
            if (f <= 40 && f - 1 >= shown && f - 1 < decoded) {
                cur ^= 1;
                compose(cur, f - 1);
                k_set_copper(cop[cur]);
                shown = f;
            }
        }
        if (decoded < 40) {
            const u16 *ix = pam_index + decoded * 6;
            if (ix[5])
                k_unpack(pam_packed + (((u32)ix[0] << 16) | ix[1]), bands + (((u32)ix[2] << 16) | ix[3]));
            decoded++;
        } else
            k_waitvbl();
    }
    /* end on white */
    fade_level = 63;
    k_waitvbl();
    k_vbl_hook = NULL;
    {
        static u32 white[4] = { CMOVE(R_BPLCON0, 0x0200), CMOVE(R_COLOR00, 0xfff), CEND, CEND };
        k_set_copper(white);
    }
}
