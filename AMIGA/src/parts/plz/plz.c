/*
 * PLZPART - the copper plasma (PLZPART/PLZ.C, ASMYT.ASM, COPPER.ASM).
 *
 * Amiga version: the plasma is a copper chunky screen, 40 x 70 cells of
 * 8x2 pixels written as 12 bit colours, so the 256 colour gradients of the
 * original survive.  The two plasmas the original interleaves (k and l
 * parameter sets) go to COLOR00 and COLOR01, alternately every 8 pixels,
 * over a 1 pixel checkerboard bitplane - the eye mixes them like on VGA.  Palette changes drop the plasma down
 * the screen and fade the next one in, timed by the music as before.
 */
#include "kernel.h"
#include "plz_data.h"

#define LINE0 0x48
#define NLINES 200
#define LSLOT 180                       /* bytes per copper line */
#define PROWS 70                        /* plasma rows of 2 lines */
#define PTOP 30                         /* top line of the plasma */
#define HPOS 0x38

void plz_even(const u16 *p1, const s32 *ta, u16 *cop, const s16 *tb,
              const u16 *p2, const u16 *pal);
void plz_odd(const u16 *p1, const s32 *ta, u16 *cop, const s16 *tb,
             const u16 *p2, const u16 *pal);
void cube_main(void);

static u32 *cop;
static u8 *lines;                       /* first line slot */
static u16 coltab[257];
static u16 bg = 0xfff;               /* palette colour 0: outside the plasma */

/* plasma parameters (moved every 70 Hz frame by the copper routine) */
static s16 k1 = 3500, k2 = 2300, k3 = 3900, k4 = 3670;
static s16 l1 = 1000, l2 = 2000, l3 = 3000, l4 = 4000;
static s16 ik[4], il[4];
static const s16 inittable[6][8] = {
    {1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670},
    {1000, 2000, 4000, 4000, 1500, 2300, 3900, 1670},
    {3500, 1000, 3000, 1000, 3500, 3300, 2900, 2670},
    {1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670},
    {1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670},
    {1000, 2000, 3000, 4000, 3500, 2300, 3900, 3670}};
static const s32 timetable[6] = {
    64 * 6 * 2 - 45, 64 * 6 * 4 - 45, 64 * 6 * 5 - 45, 64 * 6 * 6 - 45, 64 * 6 * 7 + 90, 0};

/* copper "drop" and palette fade state (COPPER.ASM do_drop) */
static int cop_drop, top = PTOP, curpal, newpal;
static u16 fade[768];                   /* 8.8 fixed point, like fadepal */
static const u8 *fadesrc;
static int fadeneg;                     /* first palette: fades from white */

static void set_coltab(void)
{
    int i;
    for (i = 0; i < 256; i++) {
        const u16 *f = fade + i * 3;
        coltab[i] = (((f[0] >> 8) & 63) >> 2 << 8) | (((f[1] >> 8) & 63) >> 2 << 4) |
                    (((f[2] >> 8) & 63) >> 2);
    }
    coltab[256] = coltab[0];
}

static void fade_step(void)
{
    int i;
    for (i = 0; i < 768; i++) {
        int p = fadesrc[i];
        fade[i] += fadeneg ? (u16)((p - 63) * 2) : (u16)(p * 8);
    }
}

/* one 70 Hz frame of the copper interrupt */
static void tick70(void)
{
    k1 = (k1 - 3) & 4095; k2 = (k2 - 2) & 4095; k3 = (k3 + 1) & 4095; k4 = (k4 + 2) & 4095;
    l1 = (l1 - 1) & 4095; l2 = (l2 - 2) & 4095; l3 = (l3 + 2) & 4095; l4 = (l4 + 3) & 4095;
    top = PTOP;
    if (!cop_drop) return;
    cop_drop++;
    if (cop_drop <= 64) {
        int c = cop_drop;
        top = (c * c / 4 * 43 / 128 + 60) / 2;
        return;
    }
    if (cop_drop >= 256) { cop_drop = 0; return; }
    if (cop_drop < 128 && cop_drop > 64 + 32) return;
    if (cop_drop == 65) {
        top = NLINES;                   /* hidden for a frame */
        k1 = ik[0]; k2 = ik[1]; k3 = ik[2]; k4 = ik[3];
        l1 = il[0]; l2 = il[1]; l3 = il[2]; l4 = il[3];
        return;
    }
    fade_step();
    newpal = 1;
}

/* parts: bit 0 = k plasma (even cells), bit 1 = l plasma (odd cells) */
static void draw(int parts)
{
    int r, y;
    u8 *ln = lines + top * LSLOT;
    for (r = 0; r < PROWS && top + r * 2 + 1 < NLINES; r++, ln += 2 * LSLOT) {
        int R, S2, Q;
        y = r * 4;
        /* k plasma in COLOR00 (even cells), l plasma in COLOR01 (odd) */
        R = k2 + y + 320;
        S2 = k3 + 2 * y + 320;
        Q = k4 + y;
        if (parts & 1)
            plz_even((const u16 *)((const u8 *)pw + 2 * (k1 + 10 * R)), ta + R, (u16 *)ln,
                 tb + Q, pw + S2 + Q / 12, coltab);
        R = l2 + y + 320;
        S2 = l3 + 2 * y + 320;
        Q = l4 + y;
        if (parts & 2)
            plz_odd((const u16 *)((const u8 *)pw + 2 * (l1 + 10 * R)), ta + R, (u16 *)ln,
                tb + Q, pw + S2 + Q / 12, coltab);
        /* COLOR01 for the first 8 pixels: set at the end of the line
         * above (in the border, where COLOR01 does not show) */
        if (parts & 2) {
            ((u16 *)(ln - LSLOT))[43 * 2 + 1] = ((u16 *)ln)[7];
            ((u16 *)ln)[43 * 2 + 1] = ((u16 *)ln)[7];
        }
    }
}

static void clear_lines(int a, int b)
{
    int l, x;
    if (a < 0) a = 0;
    if (b > NLINES) b = NLINES;
    for (l = a; l < b; l++) {
        u16 *p = (u16 *)(lines + l * LSLOT) + 3;
        for (x = 0; x < 41; x++, p += 2)
            *p = bg;
        if (l > 0)
            ((u16 *)(lines + (l - 1) * LSLOT))[43 * 2 + 1] = bg;
    }
}

static u32 *hdrcol;

static void set_bg(u16 c)
{
    int l;
    bg = c;
    hdrcol[0] = CMOVE(R_COLOR00, c);
    hdrcol[1] = CMOVE(R_COLOR00 + 2, c);
    for (l = 0; l < NLINES; l++) {
        u16 *p = (u16 *)(lines + l * LSLOT);
        p[42 * 2 + 1] = c;
        p[43 * 2 + 1] = c;
    }
    clear_lines(0, top);
    clear_lines(top + PROWS * 2, NLINES);
}

void part_main(void)
{
    u32 *c;
    u8 *plane;
    int i, ttptr = 0, oldtop = PTOP;
    u32 t0, ph = 0;
    /* checkerboard plane: COLOR00 and COLOR01 mix like the original's
     * interleaved plasmas */
    plane = k_alloc(40 * NLINES);
    for (i = 0; i < 40 * NLINES; i++)
        plane[i] = (i / 40) & 1 ? 0xaa : 0x55;
    cop = k_alloc(32 * 4 + NLINES * LSLOT + 16);
    c = cop;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPL1PTH, (u32)plane >> 16);
    *c++ = CMOVE(R_BPL1PTH + 2, (u32)plane & 0xffff);
    hdrcol = c;
    *c++ = CMOVE(R_COLOR00, 0xfff);
    *c++ = CMOVE(R_COLOR00 + 2, 0xfff);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CWAIT(LINE0 - 1, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x1200);
    lines = (u8 *)c;
    for (i = 0; i < NLINES; i++) {
        int k;
        *c++ = CWAIT((LINE0 + i) & 0xff, HPOS);
        *c++ = CMOVE(0x1fe, 0);
        for (k = 0; k < 40; k++)
            *c++ = CMOVE(R_COLOR00 + (k & 1) * 2, 0xfff);
        *c++ = CMOVE(R_COLOR00, 0xfff);
        *c++ = CMOVE(R_COLOR00 + 2, 0xfff);
        *c++ = (LINE0 + i == 255) ? CWAIT_PAL : CMOVE(0x1fe, 0);
    }
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
    for (i = 0; i < 4; i++) {
        ik[i] = inittable[0][4 + i];
        il[i] = inittable[0][i];
    }
    /* the zoomer ended on white */
    for (i = 0; i < 257; i++)
        coltab[i] = 0xfff;
    k_set_copper(cop);
    { extern volatile u32 k_dbg_phase; k_dbg_phase = 0x5300000f; }
    while (!k_exit() && k_mus_plus() < 0 && !k_mus_after(62, 0))
        k_waitvbl();
    k_mus_frame = 0;
    /* first palette: from white (the 6 bit wrap of the original) */
    memset(fade, 0, sizeof(fade));
    fadesrc = plzpal;
    fadeneg = 1;
    cop_drop = 128;
    curpal = 1;
    set_coltab();
    t0 = k_frames;
    for (;;) {
        u32 now;
        if (k_exit()) break;
        { extern volatile u32 k_dbg_phase; k_dbg_phase = 0x5100000f; }
        if (k_mus_frame > timetable[ttptr]) {
            memset(fade, 0, sizeof(fade));
            fadesrc = plzpal + 768 * curpal++;
            fadeneg = 0;
            cop_drop = 1;
            ttptr++;
            for (i = 0; i < 4; i++) {
                il[i] = inittable[ttptr][i];
                ik[i] = inittable[ttptr][4 + i];
            }
        }
        if (curpal == 5 && cop_drop > 64) break;
        /* advance the 70 Hz copper routine */
        now = (k_frames - t0) * 7 / 5;
        while (ph < now) {
            tick70();
            ph++;
        }
        if (newpal) {
            set_coltab();
            newpal = 0;
            if (coltab[0] != bg)
                set_bg(coltab[0]);
        }
        if (top > oldtop)
            clear_lines(oldtop, top);
        if (top < oldtop)
            clear_lines(PTOP + PROWS * 2, NLINES);
        /* the two plasmas update on alternate frames (both when moving) */
        draw(top != oldtop ? 3 : 1 << (k_frames & 1));
        oldtop = top;
        k_waitvbl();
    }
    set_bg(0);
    clear_lines(0, NLINES);
    k_waitvbl();
    cube_main();
}
