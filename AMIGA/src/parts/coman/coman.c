/*
 * 3DSINFLD - flight over a landscape of sine waves (COMAN/MAIN.C, the
 * generated ray marcher THELOOP.INC from DOLOOP.C).
 *
 * Amiga version: a copper chunky screen of 40 x 70 cells (8x2 pixels) in
 * 12 bit colour; each column is ray marched through the two wave height
 * tables in 64 steps (192 in the original, its far half doubled).
 */
#include "kernel.h"
#include "coman_data.h"

#define LINE0 0x48
#define LSLOT 172
#define NSLOT 360               /* 200 lines + room below for the rise */
#define TOP0 52                 /* landscape top line (vbuf row 60) */

void vox_col(int x, int y, int xs, int ys, u16 *cell, u16 *top,
             const s16 *w1mid, const s16 *w2mid, const u16 *cols, s32 dir);

static u16 coltab[128];

void part_main(void)
{
    u32 *cop, *c;
    u8 *lines;
    int i, a, startrise = 160, frame = 0, rot = 0, rot2 = 0;
    int xwav = 0, ywav = 0, top, oldtop = -1;
    u32 t0, ph = 0, cam = 0;
    int rsin = 0, rcos = 0, rsin2 = 0, rcos2 = 0;
    for (i = 0; i < 128; i++)
        coltab[i] = ((cpal[i * 3] >> 2) << 8) | ((cpal[i * 3 + 1] >> 2) << 4) | (cpal[i * 3 + 2] >> 2);
    cop = k_alloc(8 * 4 + NSLOT * LSLOT);
    c = cop;
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CMOVE(R_COLOR00, 0);
    lines = (u8 *)c;
    for (i = 0; i < NSLOT; i++) {
        int k;
        /* slots from line 200 on are never executed (after CEND) */
        *c++ = CWAIT((LINE0 + i) & 0xff, 0x3c);
        for (k = 0; k < 40; k++)
            *c++ = CMOVE(R_COLOR00, 0);
        *c++ = CMOVE(R_COLOR00, 0);
        *c++ = i == 199 ? CEND : (LINE0 + i == 255) ? CWAIT_PAL : CMOVE(0x1fe, 0);
    }
    k_set_copper(cop);
    k_waitvbl();
    while (!k_exit() && k_mus_plus() < 0)
        k_waitvbl();
    t0 = k_frames;
    for (;;) {
        u32 now = (k_frames - t0) * 7 / 5;
        int mp = k_mus_plus();
        if (k_exit() || frame >= 4444) break;
        if (mp > -8 && mp < 0) break;
        /* 70 Hz: rise at the start, sink at the end */
        while (ph < now) {
            ph++;
            frame++;
            if (mp > -30 && mp < 0) {
                if (startrise < 160) startrise++;
            }
            if (frame < 400 && startrise > 0)
                startrise--;
        }
        /* camera: the original moves once per drawn frame (35 Hz) */
        while (cam < now / 2) {
            int r;
            cam++;
            rot2 += 4;
            rot += csin[rot2 & 1023] / 15;
            r = rot >> 3;
            rsin = csin[r & 1023];
            rcos = csin[(r + 256) & 1023];
            rsin2 = csin[(r + 177) & 1023];
            rcos2 = csin[(r + 177 + 256) & 1023];
            xwav += (160 * rsin / 256) * 4;
            ywav += (160 * rcos2 / 256) * 4;
        }
        top = TOP0 + startrise;
        if (top > oldtop) {
            /* the landscape sinks: blank the lines it left */
            int l, k;
            for (l = oldtop < 0 ? 0 : oldtop; l < top && l < 200; l++) {
                u16 *p = (u16 *)(lines + l * LSLOT) + 3;
                for (k = 0; k < 40; k++, p += 2)
                    *p = 0;
            }
        }
        oldtop = top;
        for (a = 0; a < 40; a++) {
            int x = 4 * a + 2 - 80;
            int xa = (x * rcos + 160 * rsin) / 256;
            int ya = (160 * rcos2 - x * rsin2) / 256;
            u16 *bottom = (u16 *)(lines + (top + 138) * LSLOT) + 3 + 2 * a;
            u16 *ctop = (u16 *)(lines + top * LSLOT) + 3 + 2 * a;
            vox_col((xwav & ~1) ^ 0x8000, (ywav & ~1) ^ 0x8000, (xa & ~1) * 2, (ya & ~1) * 2,
                    bottom, ctop, (const s16 *)((const u8 *)w1dta + 32768),
                    (const s16 *)((const u8 *)w2dta + 32768), coltab, -(200 - 70) * 2560L);
        }
        k_waitvbl();
    }
    for (i = 0; i < 200; i++) {
        u16 *p = (u16 *)(lines + i * LSLOT) + 3;
        int k;
        for (k = 0; k < 40; k++, p += 2)
            *p = 0;
    }
    k_waitvbl();
}
