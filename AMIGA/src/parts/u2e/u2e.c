/*
 * U2E - second vector part: the flight through the city (VISU/C/U2E.C).
 * Uses the VISU engine port.  Starts from white: the letterbox bars fade
 * to black while the window stays white, then the city is drawn once the
 * music passes order 18; at the end the screen fades to white.
 */
#include "kernel.h"
#include "../../common/visu3d.c"
#include "u2e_scene.h"

#define H 150
#define LINE0 0x48
#define TOP (LINE0 + 25)
#define PSIZE (40 * H)

static u8 *buf[2];
static u8 *scratch, *scratch2;
static u32 *cop[2];
static u16 *colp[2];
static struct v3scene sc;
static u16 pal[32];

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
        u32 a = (u32)buf[b] + i * PSIZE;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    colp[b] = (u16 *)c;
    for (i = 0; i < 32; i++)
        *c++ = CMOVE(R_COLOR00 + i * 2, 0xfff);
    *c++ = CWAIT(TOP, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x5200);
    *c++ = CWAIT(TOP + H, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

/* level 0 = palette .. 64 = white */
static void set_cols(int white)
{
    int i, b;
    for (i = 0; i < 32; i++) {
        u16 c = pal[i];
        int r = (c >> 8) & 15, g = (c >> 4) & 15, bl = c & 15;
        r += ((15 - r) * white) >> 6;
        g += ((15 - g) * white) >> 6;
        bl += ((15 - bl) * white) >> 6;
        for (b = 0; b < 2; b++)
            colp[b][i * 2 + 1] = (r << 8) | (g << 4) | bl;
    }
}

void part_main(void)
{
    struct v3screen scr;
    u32 *lb, *c, t0;
    int cur = 0, i;
    s32 frame = 0;
    buf[0] = k_alloc(PSIZE * 5);
    buf[1] = k_alloc(PSIZE * 5);
    scratch = k_alloc(PSIZE + 80);
    scratch2 = k_alloc(PSIZE + 80);
    memset(scratch, 0, PSIZE + 80);
    memset(scratch2, 0, PSIZE + 80);
    cop[0] = k_alloc(80 * 4);
    cop[1] = k_alloc(80 * 4);
    build_copper(0);
    build_copper(1);
    pal[0] = 0;
    for (i = 0; i < 31; i++)
        pal[i + 1] = u2e_cols[i];

    sc.objs = u2e_objs;
    sc.inst = u2e_inst;
    sc.conum = U2E_CONUM;
    sc.streams = u2e_streams;
    sc.nstreams = U2E_NSTREAMS;
    sc.kind = U2E_KIND;
    sc.colmap = u2e_colmap;
    sc.iflags = u2e_iflags;
    v3_init(&sc);
    v3_step(&sc);

    /* white screen, then the letterbox bars fade to black (the window
     * stays white): COLOR00 per region */
    lb = k_alloc(16 * 4);
    c = lb;
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CMOVE(R_COLOR00, 0xfff);
    *c++ = CWAIT(TOP, 0x07);
    *c++ = CMOVE(R_COLOR00, 0xfff);
    *c++ = CWAIT(TOP + H, 0x07);
    *c++ = CMOVE(R_COLOR00, 0xfff);
    *c++ = CEND;
    k_set_copper(lb);
    t0 = k_frames;
    while (!k_exit()) {
        int f = (k_frames - t0) * 7 / 5, l;
        u16 v;
        if (f >= 32 + 33) break;
        l = f < 32 ? 63 : 63 - (f - 32) * 2;
        if (l < 0) l = 0;
        v = (l >> 2) * 0x111;
        lb[1] = CMOVE(R_COLOR00, v);
        lb[5] = CMOVE(R_COLOR00, v);
        k_waitvbl();
    }
    lb[1] = CMOVE(R_COLOR00, 0);
    lb[5] = CMOVE(R_COLOR00, 0);
    /* the city starts after order 18 */
    while (!k_exit() && k_mus_order <= 18)
        k_waitvbl();
    set_cols(0);

    scr.scratch = scratch;
    scr.scratch2 = scratch2;
    scr.h = H;
    scr.addx = 159;
    scr.addy = 74;
    scr.clipy0 = 0;
    scr.clipy1 = H;
    t0 = k_frames;
    while (!k_exit()) {
        s32 target;
        cur ^= 1;
        k_blit_clear(buf[cur], 0, 20, H * 5);
        k_wait_blitter();
        for (i = 0; i < 5; i++)
            scr.planes[i] = buf[cur] + i * PSIZE;
        v3_draw(&sc, &scr);
        k_wait_blitter();
        k_set_copper(cop[cur]);
        k_waitvbl();
        /* the animation runs at 35 frames per second */
        target = (k_frames - t0) * 7 / 10;
#ifdef V3PROF
        target = frame + 2;         /* benchmark: fixed frames */
#endif
        while (frame < target && !sc.wrapped) {
            v3_step(&sc);
            frame++;
        }
        if (sc.wrapped)
            break;
    }
    /* fade to white */
    t0 = k_frames;
    for (;;) {
        int f = (k_frames - t0) * 7 / 5;
        if (f >= 16 || k_exit()) break;
        set_cols(f * 4 + 4);
        k_waitvbl();
    }
    set_cols(64);
    k_waitvbl();
}
