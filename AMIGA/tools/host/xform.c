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
