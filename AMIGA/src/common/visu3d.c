/*
 * Amiga port of the VISU vector engine (Sami Tammilehto / Future Crew).
 *
 * Mirrors the original pipeline: animation stream -> object matrices,
 * camera applied, objects sorted by distance, per object a precalculated
 * polygon order list is chosen, back faces are culled, flat shading from a
 * fixed light vector, near plane clipping and projection.  Polygons are
 * filled with the blitter: the CPU marks the edges in a scratch plane, the
 * blitter fills it and stamps it into the 5 bitplanes in the right colour.
 *
 * Coordinates are scaled by 1/8 so object vertices fit in 16 bits.
 */
#include "visu3d.h"

#define SHIFT 3
#define ZMIN (512 >> SHIFT)
#define BPR 40

int v3_polys_drawn;
#define wblit k_wait_blitter

static const s16 light[3] = { 12118, 10603, 3030 };

#include "avistan.h"

/* ------------------------------------------------------------ animation */
void v3_init(struct v3scene *s)
{
    s->si = 0;
    s->sp = s->streams[0];
    s->wrapped = 0;
    s->fov = 0;
    memset(s->r0, 0, sizeof(s->r0));
    memset(s->on, 0, sizeof(s->on));
}

static s32 lsget(struct v3scene *s, int f)
{
    const u8 *p = s->sp;
    s32 l;
    switch (f & 3) {
    case 0: return 0;
    case 1: l = (s8)p[0]; s->sp += 1; return l;
    case 2: l = p[0] | ((s32)(s8)p[1] << 8); s->sp += 2; return l;
    default:
        l = p[0] | ((s32)p[1] << 8) | ((s32)p[2] << 16) | ((s32)(s8)p[3] << 24);
        s->sp += 4;
        return l;
    }
}

void v3_step(struct v3scene *s)
{
    int onum = 0;
    s->frame++;
    for (;;) {
        int a = *s->sp++, b;
        u32 pf;
        s32 *r;
        if (a == 0xff) {
            a = *s->sp++;
            if (a <= 0x7f) {
                s->fov = a << 8;
                return;
            } else if (a == 0xff) {
                if (++s->si >= s->nstreams) {
                    s->si = 0;
                    s->wrapped = 1;
                }
                s->sp = s->streams[s->si];
                memset(s->r0, 0, sizeof(s->r0));
                continue;
            }
        }
        if ((a & 0xc0) == 0xc0) {
            onum = (a & 0x3f) << 4;
            a = *s->sp++;
        }
        onum = (onum & 0xff0) | (a & 0xf);
        if ((a & 0xc0) == 0x80) s->on[onum] = 1;
        else if ((a & 0xc0) == 0x40) s->on[onum] = 0;
        r = s->r0[onum < V3_MAXINST ? onum : 0];
        pf = 0;
        switch (a & 0x30) {
        case 0x10: pf = s->sp[0]; s->sp += 1; break;
        case 0x20: pf = s->sp[0] | (s->sp[1] << 8); s->sp += 2; break;
        case 0x30: pf = s->sp[0] | (s->sp[1] << 8) | ((u32)s->sp[2] << 16); s->sp += 3; break;
        }
        r[9] += lsget(s, pf);
        r[10] += lsget(s, pf >> 2);
        r[11] += lsget(s, pf >> 4);
        if (pf & 0x40) {
            for (b = 0; b < 9; b++)
                if (pf & (0x80 << b)) r[b] += lsget(s, 2);
        } else {
            for (b = 0; b < 9; b++)
                if (pf & (0x80 << b)) r[b] += lsget(s, 1);
        }
    }
}

/* ------------------------------------------------------------ maths */
#define mul16(a, b) muls16((a), (b))

/* (a * m) >> 14 for a 32 bit a and 16 bit m without 64 bit maths */
static inline s32 mul32x16(s32 a, s16 m)
{
    s32 hi = a >> 14, lo = a & 0x3fff;
    /* |hi| < 32768 for |a| < 2^29 */
    return muls16(hi, m) + (muls16(lo, m) >> 14);
}

/* n / d for d > 0, fast path with divs.w */
static inline s32 sdiv(s32 n, s32 d)
{
    while (d > 32767) {
        d >>= 1;
        n >>= 1;
    }
    if (n >= (d << 15) || n <= -(d << 15))
        return n > 0 ? 32767 : -32767;
    {
        s16 q;
#ifdef __m68k__
        __asm__("divs.w %2,%0" : "=d"(q) : "0"(n), "d"((s16)d) : "cc");
#else
        q = n / d;
#endif
        return q;
    }
}

struct mat { s16 m[9]; s32 x, y, z; };

static s32 mulx, muly;
static struct mat W;            /* current object world matrix */
struct vout { s32 x, y, z; s16 sx, sy; };
struct xf { s16 m[9]; s16 pad; s32 tx, ty, tz, mulx, muly, addx, addy, zmin; };
void v3_xform_idx(const s16 *v, const u16 *idx, int n, const struct xf *xf, struct vout *out);
static struct vout vo[V3_MAXV];
static s16 lx, ly, lz;          /* light in object space */

static void set_fov(int fov, int half)
{
    int bx = fov >> 1;
    if (bx < 8 * 64) bx = 8 * 64;
    if (bx > 16383) bx = 16383;
    mulx = (half * (s32)avistan[bx >> 6]) >> 8;
    if (mulx > 511) mulx = 511;
    muly = (mulx * 225) >> 8;
}

static void world(const s32 *cam, const s32 *obj)
{
    int r, c;
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            W.m[r * 3 + c] = (mul16(cam[r * 3], obj[c]) + mul16(cam[r * 3 + 1], obj[3 + c]) +
                              mul16(cam[r * 3 + 2], obj[6 + c])) >> 14;
    {
        s32 ox = obj[9] >> SHIFT, oy = obj[10] >> SHIFT, oz = obj[11] >> SHIFT;
        W.x = mul32x16(ox, cam[0]) + mul32x16(oy, cam[1]) + mul32x16(oz, cam[2]) + (cam[9] >> SHIFT);
        W.y = mul32x16(ox, cam[3]) + mul32x16(oy, cam[4]) + mul32x16(oz, cam[5]) + (cam[10] >> SHIFT);
        W.z = mul32x16(ox, cam[6]) + mul32x16(oy, cam[7]) + mul32x16(oz, cam[8]) + (cam[11] >> SHIFT);
    }
}

static s32 single_z(const struct v3obj *o, int v)
{
    const s16 *p = o->v + v * 3;
    return ((mul16(W.m[6], p[0]) + mul16(W.m[7], p[1]) + mul16(W.m[8], p[2])) >> 14) + W.z;
}

static struct v3screen *S;

static struct xf xf;
static u16 vstamp[V3_MAXV], stamp = 1;
static u16 vlist[V3_MAXV];
static s32 cx, cy, cz;          /* camera position in object space */

/* per object setup: transform matrix, light and camera in object space */
static void setup_object(void)
{
    int i;
    /* rows 0/1 premultiplied with the projection factors (mulx < 512) */
    for (i = 0; i < 3; i++) {
        xf.m[i] = (mul16(W.m[i], mulx)) >> 8;
        xf.m[3 + i] = (mul16(W.m[3 + i], muly)) >> 8;
        xf.m[6 + i] = W.m[6 + i];
    }
    xf.tx = W.x * mulx;
    xf.ty = W.y * muly;
    xf.tz = W.z;
    xf.mulx = mulx;
    xf.muly = muly;
    xf.addx = S->addx;
    xf.addy = S->addy;
    xf.zmin = ZMIN;
    lx = (mul16(W.m[0], light[0]) + mul16(W.m[3], light[1]) + mul16(W.m[6], light[2])) >> 14;
    ly = (mul16(W.m[1], light[0]) + mul16(W.m[4], light[1]) + mul16(W.m[7], light[2])) >> 14;
    lz = (mul16(W.m[2], light[0]) + mul16(W.m[5], light[1]) + mul16(W.m[8], light[2])) >> 14;
    /* camera (origin) in object space: -M^T T */
    {
        s32 x = -(mul32x16(W.x, W.m[0]) + mul32x16(W.y, W.m[3]) + mul32x16(W.z, W.m[6]));
        s32 y = -(mul32x16(W.x, W.m[1]) + mul32x16(W.y, W.m[4]) + mul32x16(W.z, W.m[7]));
        s32 z = -(mul32x16(W.x, W.m[2]) + mul32x16(W.y, W.m[5]) + mul32x16(W.z, W.m[8]));
        cx = x; cy = y; cz = z;
    }
}

/* ------------------------------------------------------------ filling */
static const u8 bitmask[8] = { 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01 };

/* Small polygons: CPU span fill straight into the bitplanes (convex
 * polygons: one span per row from the min/max edge x). */
static s16 spl[256], spr[256];

static void fill_cpu(struct v3screen *scr, int n, const s16 *xs, const s16 *ys, int color,
                     int y0, int y1)
{
    int i, y;
    for (y = y0; y < y1; y++) {
        spl[y] = 32767;
        spr[y] = -32768;
    }
    for (i = 0; i < n; i++) {
        int xa = xs[i], ya = ys[i], xb, yb, j = i + 1 == n ? 0 : i + 1;
        int ye, dy, q, r, err, x;
        xb = xs[j];
        yb = ys[j];
        if (ya > yb) {
            int t = xa; xa = xb; xb = t;
            t = ya; ya = yb; yb = t;
        }
        dy = yb - ya;
        if (dy == 0) {
            if (ya >= y0 && ya < y1) {
                if (xa < spl[ya]) spl[ya] = xa;
                if (xb < spl[ya]) spl[ya] = xb;
                if (xa > spr[ya]) spr[ya] = xa;
                if (xb > spr[ya]) spr[ya] = xb;
            }
            continue;
        }
        {
            s32 qr, num = xb - xa;
#ifdef __m68k__
            __asm__("divs.w %2,%0" : "=d"(qr) : "0"(num), "d"((s16)dy) : "cc");
            q = (s16)qr;
            r = (s16)(qr >> 16);
#else
            q = num / dy;
            r = num % dy;
            (void)qr;
#endif
        }
        if (r < 0) { q--; r += dy; }
        x = xa;
        err = dy >> 1;
        y = ya;
        ye = yb + 1;            /* inclusive: spans include both edge rows */
        if (ye > y1) ye = y1;
        for (; y < ye; y++) {
            if (y >= y0) {
                if (x < spl[y]) spl[y] = x;
                if (x > spr[y]) spr[y] = x;
            }
            x += q;
            err += r;
            if (err >= dy) {
                err -= dy;
                x++;
            }
        }
    }
    for (y = y0; y < y1; y++) {
        int xl = spl[y], xr = spr[y], p, w0, w1;
        u16 fm, lm;
        if (xl > xr) continue;
        if (xl < 0) xl = 0;
        if (xr > 319) xr = 319;
        if (xl > xr) continue;
        w0 = xl >> 4;
        w1 = xr >> 4;
        fm = 0xffff >> (xl & 15);
        lm = 0xffff << (15 - (xr & 15));
        if (w0 == w1) fm &= lm;
        for (p = 0; p < 5; p++) {
            u16 *d = (u16 *)(scr->planes[p] + y * BPR) + w0;
            if ((color >> p) & 1) {
                if (w0 == w1) *d |= fm;
                else {
                    int k = w1 - w0 - 1;
                    *d++ |= fm;
                    while (k-- > 0) *d++ = 0xffff;
                    *d |= lm;
                }
            } else {
                if (w0 == w1) *d &= ~fm;
                else {
                    int k = w1 - w0 - 1;
                    *d++ &= ~fm;
                    while (k-- > 0) *d++ = 0;
                    *d &= ~lm;
                }
            }
        }
    }
}

void v3_fill(struct v3screen *scr, int n, const s16 *xs, const s16 *ys, int color)
{
#ifdef V3_HOOK_FILL
    V3_HOOK_FILL(n, xs, ys, color);       /* host test harness */
#endif
    int i, ymin = 32767, ymax = -32768, xmin = 32767, xmax = -32768;
    int y0, y1, wx0, wx1, w, p;
    u8 *sc = scr->scratch;
    for (i = 0; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
        if (xs[i] < xmin) xmin = xs[i];
        if (xs[i] > xmax) xmax = xs[i];
    }
    y0 = ymin < scr->clipy0 ? scr->clipy0 : ymin;
    y1 = ymax > scr->clipy1 ? scr->clipy1 : ymax;
    if (y0 >= y1 || xmax < 0 || xmin > 319)
        return;
    if (xmin < 0) xmin = 0;
    if (xmax > 319) xmax = 319;
    if ((y1 - y0) * ((xmax >> 4) - (xmin >> 4) + 1) <= 16) {
        /* small: CPU spans (the blitter may still be clearing the scratch
         * plane of the previous polygon, which does not matter here, but
         * it may also still be stamping into the bitplanes) */
        wblit();
        fill_cpu(scr, n, xs, ys, color, y0, y1 + 1 > scr->clipy1 ? scr->clipy1 : y1 + 1);
        v3_polys_drawn++;
        return;
    }
    wblit();
    /* mark edges: one toggled pixel per row, rows [ytop, ybottom) */
    for (i = 0; i < n; i++) {
        int xa = xs[i], ya = ys[i], xb, yb, j = i + 1 == n ? 0 : i + 1;
        int y, ye, dy, q, r, err, x;
        u8 *row;
        xb = xs[j];
        yb = ys[j];
        if (ya == yb)
            continue;
        if (ya > yb) {
            int t = xa; xa = xb; xb = t;
            t = ya; ya = yb; yb = t;
        }
        dy = yb - ya;
        {
            /* x steps by q + r/dy per row (one divs.w) */
            s32 qr;
            s32 num = xb - xa;
#ifdef __m68k__
            __asm__("divs.w %2,%0" : "=d"(qr) : "0"(num), "d"((s16)dy) : "cc");
            q = (s16)qr;
            r = (s16)(qr >> 16);
#else
            q = num / dy;
            r = num % dy;
            (void)qr;
#endif
        }
        if (r < 0) { q--; r += dy; }
        x = xa;
        err = dy >> 1;
        y = ya;
        ye = yb;
        if (y < y0) {
            /* advance to the first visible row */
            s32 k = y0 - y;
            s32 tot = (s32)r * k + err;
            x += q * k + tot / dy;
            err = tot % dy;
            y = y0;
        }
        if (ye > y1) ye = y1;
        row = sc + y * BPR;
        for (; y < ye; y++, row += BPR) {
            int xi = x;
            if (xi < 0) xi = 0;
            if (xi > 319) xi = 319;
            row[xi >> 3] ^= bitmask[xi & 7];
            x += q;
            err += r;
            if (err >= dy) {
                err -= dy;
                x++;
            }
        }
    }
    wx0 = xmin >> 4;
    wx1 = xmax >> 4;
    w = wx1 - wx0 + 1;
    /* blitter fill, descending */
    {
        u8 *last = sc + (y1 - 1) * BPR + wx1 * 2;
        BLTCON0 = 0x09f0;
        BLTCON1 = 0x000a;              /* IFE | DESC */
        BLTAFWM = 0xffff;
        BLTALWM = 0xffff;
        BLTAPT = (u32)last;
        BLTDPT = (u32)last;
        BLTAMOD = BPR - 2 * w;
        BLTDMOD = BPR - 2 * w;
        BLTSIZE = ((y1 - y0) << 6) | w;
    }
    /* stamp into the bitplanes */
    {
        u32 off = y0 * BPR + wx0 * 2;
        u16 size = ((y1 - y0) << 6) | w;
        s16 mod = BPR - 2 * w;
        for (p = 0; p < 5; p++) {
            u8 *d = scr->planes[p] + off;
            wblit();
            BLTCON0 = 0x0b00 | ((color >> p) & 1 ? 0xfa : 0x0a);
            BLTCON1 = 0;
            BLTAPT = (u32)(sc + off);
            BLTCPT = (u32)d;
            BLTDPT = (u32)d;
            BLTAMOD = mod;
            BLTCMOD = mod;
            BLTDMOD = mod;
            BLTSIZE = size;
        }
        wblit();
        BLTCON0 = 0x0100;
        BLTCON1 = 0;
        BLTDPT = (u32)(sc + off);
        BLTDMOD = mod;
        BLTSIZE = size;
    }
    v3_polys_drawn++;
}

/* ------------------------------------------------------------ polygons */
#define MAXSIDES 16

static int shade_of(int flags, const s16 *nn)
{
    int sh = flags & 0x0c, d;
    if (!sh)
        d = 0;
    else {
        d = (mul16(nn[0], lx) + mul16(nn[1], ly) + mul16(nn[2], lz)) >> 16;
        d = (d >> 5) + 128;
        if (d < 0) d = 0;
        if (d > 255) d = 255;
        d >>= 6 - (sh >> 2);
    }
    if (d < 1) d = 1;
    if (d > 30) d = 30;
    return d;
}

/* object space back face test (same test as the original, done before
 * any vertex is transformed) */
static inline int facing(const struct v3obj *o, const u16 *pd)
{
    const s16 *nn, *v;
    s32 d;
    if ((pd[0] >> 8) & 0x02)
        return 1;
    nn = o->n + pd[2] * 3;
    v = o->v + pd[3] * 3;
    /* full precision: big objects seen nearly edge on (U2A's mother
     * ship) need it, 1/16 units flipped the sign */
    d = mul32x16(v[0] - cx, nn[0]) + mul32x16(v[1] - cy, nn[1]) + mul32x16(v[2] - cz, nn[2]);
#ifdef V3_DEBUG_FACING
    V3_DEBUG_FACING(o, pd, d);
#endif
    return d < 0;
}

/* d * t / 256 for 0 <= t <= 256 and |d| < 2^30 */
static inline s32 lerp256(s32 d, s32 t)
{
    return (d >> 8) * t + (((d & 255) * t) >> 8);
}

static void draw_poly(struct v3scene *s, const struct v3obj *o, const u16 *pd)
{
    int sides = pd[0] & 0xff, flags = pd[0] >> 8, i, near = 0;
    const u16 *vi = pd + 3;
    const s16 *nn = o->n + pd[2] * 3;
    s16 xs[MAXSIDES * 2], ys[MAXSIDES * 2];
    int n = 0;
    if (sides > MAXSIDES) sides = MAXSIDES;
    for (i = 0; i < sides; i++)
        if (vo[vi[i]].z < ZMIN) near = 1;
    if (!near) {
        for (i = 0; i < sides; i++) {
            xs[i] = vo[vi[i]].sx;
            ys[i] = vo[vi[i]].sy;
        }
        n = sides;
    } else {
        /* clip against the near plane */
        for (i = 0; i < sides; i++) {
            const struct vout *a = &vo[vi[i]], *b = &vo[vi[i + 1 == sides ? 0 : i + 1]];
            int ina = a->z >= ZMIN, inb = b->z >= ZMIN;
            if (ina) {
                xs[n] = a->sx;
                ys[n] = a->sy;
                n++;
            }
            if (ina != inb) {
                s32 dz = b->z - a->z;
                s32 t = sdiv((ZMIN - a->z) << 8, dz < 0 ? -dz : dz);
                s32 x, y;
                if (dz < 0) t = -t;
                /* dx * t / 256 without overflow: big objects close to the
                 * camera (U2A's mother ship) have dx in the 10^7 range */
                x = a->x + lerp256(b->x - a->x, t);
                y = a->y + lerp256(b->y - a->y, t);
                /* x, y already scaled; ZMIN is a power of two.  Clamp
                 * like the projection does: big walls and floors close
                 * to the camera (U2E's city) would wrap around */
                x = (x >> (9 - SHIFT)) + S->addx;
                y = (y >> (9 - SHIFT)) + S->addy;
                xs[n] = x > 16000 ? 16000 : x < -16000 ? -16000 : x;
                ys[n] = y > 16000 ? 16000 : y < -16000 ? -16000 : y;
                n++;
            }
        }
        if (n < 3)
            return;
#ifdef V3_DEBUG_CLIP
        V3_DEBUG_CLIP(o, pd, n, xs, ys);
#endif
    }
    {
        int col = (pd[1] + shade_of((flags & 0x10) ? (flags | 0x0c) : flags, nn)) & 0xff;
        v3_fill(S, n, xs, ys, s->colmap[col]);
    }
}

#define MAXVIS 512
static const u16 *vis[MAXVIS];

static void draw_object(struct v3scene *s, const struct v3obj *o)
{
    int best = 0, i, nvis = 0, nv = 0;
    s32 bz = 0x7fffffff;
    const u16 *l;
    setup_object();
    if (++stamp == 0) {
        memset(vstamp, 0, sizeof(vstamp));
        stamp = 1;
    }
    /* sort vertices of the precalculated lists (transformed first) */
    for (i = 1; i < o->nl; i++) {
        int v = o->lists[i][0];
        if (vstamp[v] != stamp) {
            vstamp[v] = stamp;
            vlist[nv++] = v;
        }
    }
    {
        v3_xform_idx(o->v, vlist, nv, &xf, vo);
    }
    for (i = 1; i < o->nl; i++) {
        s32 z = vo[o->lists[i][0]].z;
        if (z < bz) {
            bz = z;
            best = i;
        }
    }
    l = o->lists[best];
    /* cull, then transform only the vertices of visible polygons */
    nv = 0;
    {
        int cnt = l[1];
        const u16 *pp = l + 2;
        while (cnt--) {
            const u16 *pd = o->pd + *pp++;
            if (!facing(o, pd))
                continue;
            if (nvis < MAXVIS)
                vis[nvis++] = pd;
            {
                int k, sides = pd[0] & 0xff;
                for (k = 0; k < sides; k++) {
                    int v = pd[3 + k];
                    if (vstamp[v] != stamp) {
                        vstamp[v] = stamp;
                        vlist[nv++] = v;
                    }
                }
            }
        }
    }
    {
        v3_xform_idx(o->v, vlist, nv, &xf, vo);
        for (i = 0; i < nvis; i++)
            draw_poly(s, o, vis[i]);
    }
}

void v3_draw(struct v3scene *s, struct v3screen *scr)
{
    static u8 order[V3_MAXINST];
    static s32 dist[V3_MAXINST];
    static struct mat mats[V3_MAXINST];
    int n = 0, a, b;
    S = scr;
    v3_polys_drawn = 0;
    set_fov(s->fov, 160);
    for (a = 1; a < s->conum; a++) {
        if (!s->on[a])
            continue;
        world(s->r0[0], s->r0[a]);
        mats[a] = W;
        dist[a] = single_z(&s->objs[s->inst[a]], s->objs[s->inst[a]].center);
        if (s->kind == 'C' && (a == 2 || a == 7 || a == 13)) dist[a] = 1000000000;
        if (s->kind == 'R' && a == 14) dist[a] = 1000000000;
        if (s->iflags) {
            if (s->iflags[a] & 1) dist[a] = 1000000000;
            if ((s->iflags[a] & 2) && s->frame > 900 && s->frame < 1100) dist[a] = 1;
        }
        /* insertion sort, far first */
        for (b = n - 1; b >= 0 && dist[a] > dist[order[b]]; b--)
            order[b + 1] = order[b];
        order[b + 1] = a;
        n++;
    }
    for (a = 0; a < n; a++) {
        int k = order[a];
        W = mats[k];
        draw_object(s, &s->objs[s->inst[k]]);
    }
}
