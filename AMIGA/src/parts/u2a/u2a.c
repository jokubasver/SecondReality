/*
 * U2A - first vector part: the mothership and fighters over the horizon.
 * Original: VISU/C/U2A.C (Psi).  Uses the VISU engine port.
 */
#include "kernel.h"
#include "../../common/visu3d.c"
#include "u2a_scene.h"
#include "u2a_bg.h"

#define H 150
#define LINE0 0x61
#define PSIZE (40 * H)

static u8 *buf[2];
static u8 *scratch;
static u32 *cop[2];
static struct v3scene sc;
static volatile int show = -1;

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
    for (i = 0; i < 16; i++)
        *c++ = CMOVE(R_COLOR00 + i * 2, bg_pal[i]);
    for (i = 0; i < 16; i++)
        *c++ = CMOVE(R_COLOR00 + 32 + i * 2, u2a_cols[i]);
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x5200);
    *c++ = CWAIT(LINE0 + H, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

static void clear_to_bg(u8 *b)
{
    static const u8 *const bgp[4] = { bg_plane0, bg_plane1, bg_plane2, bg_plane3 };
    int p;
    for (p = 0; p < 4; p++)
        k_blit_copy(bgp[p], 0, b + p * PSIZE, 0, 20, H, 0);
    k_blit_clear(b + 4 * PSIZE, 0, 20, H);
    k_wait_blitter();
}

static int mus_pos(void) { return (k_mus_order << 8) | k_mus_row; }

void part_main(void)
{
    struct v3screen scr;
    int cur = 0, i;
    u32 t0;
    s32 frame = 0;
    buf[0] = k_alloc(PSIZE * 5);
    buf[1] = k_alloc(PSIZE * 5);
    scratch = k_alloc(PSIZE + 80);
    cop[0] = k_alloc(80 * 4);
    cop[1] = k_alloc(80 * 4);
    clear_to_bg(buf[0]);
    clear_to_bg(buf[1]);
    build_copper(0);
    build_copper(1);

    sc.objs = u2a_objs;
    sc.inst = u2a_inst;
    sc.conum = U2A_CONUM;
    sc.streams = u2a_streams;
    sc.nstreams = U2A_NSTREAMS;
    sc.kind = U2A_KIND;
    sc.colmap = u2a_colmap;
    v3_init(&sc);
    v3_step(&sc);

    /* original starts at order 10 row 47 */
    while (!k_exit() && !(k_mus_order > 10 && k_mus_row > 46) && k_mus_order < 12)
        k_waitvbl();
    k_set_copper(cop[0]);

    scr.scratch = scratch;
    scr.h = H;
    scr.addx = 159;
    scr.addy = 74;
    scr.clipy0 = 0;
    scr.clipy1 = H;
    t0 = k_frames;
    while (!k_exit()) {
        s32 target;
        if (k_mus_order > 11 && k_mus_row > 54)
            break;
        if (k_mus_order > 12)
            break;
        u32 td;
        cur ^= 1;
        clear_to_bg(buf[cur]);
        for (i = 0; i < 5; i++)
            scr.planes[i] = buf[cur] + i * PSIZE;
        td = k_frames;
        v3_draw(&sc, &scr);
        k_wait_blitter();
#ifdef DEBUG
        /* frames per update, polygons drawn */
        k_debug_hex(scr.planes[4], 40, 1, 2, ((k_frames - td) << 8) | (v3_polys_drawn & 0xff));
#endif
        k_set_copper(cop[cur]);
        k_waitvbl();
        /* the animation runs at 70 frames per second */
        target = (k_frames - t0) * 7 / 5;
        while (frame < target && !sc.wrapped) {
            v3_step(&sc);
            frame++;
        }
        if (sc.wrapped)
            break;
    }
    (void)mus_pos;
}
