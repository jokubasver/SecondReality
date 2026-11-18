/* Edge toggling + blitter area fill (for XOR filled / glenz vectors). */
#ifndef XORFILL_H
#define XORFILL_H
#include "kernel.h"

/* Toggles one pixel per row along the edge, rows [ytop, ybottom) within
 * [cy0, cy1); x is clamped to [0, w-1]. */
static void xf_edge(u8 *plane, int bpr, int w, int xa, int ya, int xb, int yb, int cy0, int cy1)
{
    static const u8 bm[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };
    int dy, q, r, err, x, y, ye;
    u8 *row;
    if (ya == yb)
        return;
    if (ya > yb) {
        int t = xa; xa = xb; xb = t;
        t = ya; ya = yb; yb = t;
    }
    dy = yb - ya;
    {
        s32 qr, num = xb - xa;
        __asm__("divs.w %2,%0" : "=d"(qr) : "0"(num), "d"((s16)dy) : "cc");
        q = (s16)qr;
        r = (s16)(qr >> 16);
    }
    if (r < 0) { q--; r += dy; }
    x = xa;
    err = dy >> 1;
    y = ya;
    ye = yb > cy1 ? cy1 : yb;
    while (y < cy0 && y < ye) {
        x += q;
        err += r;
        if (err >= dy) { err -= dy; x++; }
        y++;
    }
    row = plane + muls16(y, bpr);
    for (; y < ye; y++, row += bpr) {
        int xi = x < 0 ? 0 : (x >= w ? w - 1 : x);
        row[xi >> 3] ^= bm[xi & 7];
        x += q;
        err += r;
        if (err >= dy) { err -= dy; x++; }
    }
}

/* Blitter fill (inclusive) of a rectangle in place: words [wx0, wx0+ww),
 * rows [y0, y0+h). */
static void xf_fill(u8 *plane, int bpr, int wx0, int ww, int y0, int h)
{
    u8 *last;
    if (h <= 0 || ww <= 0)
        return;
    last = plane + muls16(y0 + h - 1, bpr) + (wx0 + ww - 1) * 2;
    k_wait_blitter();
    BLTCON0 = 0x09f0;
    BLTCON1 = 0x000a;          /* IFE | DESC */
    BLTAFWM = 0xffff;
    BLTALWM = 0xffff;
    BLTAPT = (u32)last;
    BLTDPT = (u32)last;
    BLTAMOD = bpr - 2 * ww;
    BLTDMOD = bpr - 2 * ww;
    BLTSIZE = (h << 6) | ww;
}

#endif
