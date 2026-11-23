#include "kernel.h"
struct vout { s32 x, y, z; s16 sx, sy; };
struct xf { s16 m[9]; s16 pad; s32 tx, ty, tz, mulx, muly, addx, addy, zmin; };
static s32 shr14(s32 d) { return d >> 14; }
static s16 proj(s32 a, s32 z, s32 add)
{
    s32 q;
    if (z > 32767) { while (z > 32767) { z >>= 1; a >>= 1; } }
    q = a / z;
    if (q > 32767 || q < -32768 || q > 16000 || q < -16000) return a >= 0 ? 16000 : -16000;
    return (s16)(q + add);
}
void v3_xform_idx(const s16 *v, const u16 *idx, int n, const struct xf *xf, struct vout *out)
{
    while (n-- > 0) {
        int k = *idx++;
        const s16 *p = v + k * 3;
        struct vout *o = out + k;
        s32 z = shr14(p[0] * xf->m[6] + p[1] * xf->m[7] + p[2] * xf->m[8]) + xf->tz;
        s32 x = ((p[0] * xf->m[0] + p[1] * xf->m[1] + p[2] * xf->m[2]) >> 6) + xf->tx;
        s32 y = ((p[0] * xf->m[3] + p[1] * xf->m[4] + p[2] * xf->m[5]) >> 6) + xf->ty;
        o->x = x; o->y = y; o->z = z;
        if (z >= xf->zmin) { o->sx = proj(x, z, xf->addx); o->sy = proj(y, z, xf->addy); }
    }
}

void v3_edge(u8 *row, s32 x, s32 step, int count)
{
    for (; count > 0; count--, row += 40, x += step)
        row[(x >> 16) >> 3] ^= 0x80 >> ((x >> 16) & 7);
}

void v3_edge_clip(u8 *row, s32 x, s32 step, int count)
{
    for (; count > 0; count--, row += 40, x = (s32)((u32)x + (u32)step)) {
        int xi = (s16)(x >> 16);
        if (xi < 0) xi = 0;
        if (xi > 319) xi = 319;
        row[xi >> 3] ^= 0x80 >> (xi & 7);
    }
}

void v3_span_edge(s16 *spl, s16 *spr, s32 x, s32 step, int count)
{
    for (; count > 0; count--, spl++, spr++, x = (s32)((u32)x + (u32)step)) {
        s16 xi = (s16)(x >> 16);
        if (xi < *spl) *spl = xi;
        if (xi > *spr) *spr = xi;
    }
}

void v3_spans(u8 *row, s32 stride, const s16 *spl, const s16 *spr, int count, int color)
{
    int p, x;
    for (; count > 0; count--, row += 40, spl++, spr++) {
        int xl = *spl < 0 ? 0 : *spl, xr = *spr > 319 ? 319 : *spr;
        for (x = xl; x <= xr; x++)
            for (p = 0; p < 5; p++) {
                u8 *b = row + p * stride + (x >> 3);
                if ((color >> p) & 1) *b |= 0x80 >> (x & 7);
                else *b &= ~(0x80 >> (x & 7));
            }
    }
}
