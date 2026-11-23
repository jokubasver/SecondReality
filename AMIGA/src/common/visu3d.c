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
/* small polygons by CPU spans (off: the queued blitter is cheaper) */
#ifndef V3_BQ
#define V3_BQ 0
#endif
#ifndef V3_CPU_FILL
#define V3_CPU_FILL 1
#endif
#define ZMIN (512 >> SHIFT)
#define BPR 40

int v3_polys_drawn;
#ifdef V3PROF
/* profiling: raster lines spent waiting for the blitter / in v3_draw */
u32 v3_prof_wait, v3_prof_total;
static inline int v3_line(void) { return ((*(volatile u16 *)0xdff004 & 1) << 8) | (*(volatile u16 *)0xdff006 >> 8); }
static void wblit_prof(void)
{
    int a = v3_line(), b;
    k_wait_blitter();
    b = v3_line();
    v3_prof_wait += b >= a ? b - a : b + 313 - a;
}
#define wblit wblit_prof
/* section timers (colour clocks): PB(i) ... PE(i) */
static u32 psec[9], pt0[9];
static inline u32 v3_now(void)
{
    u32 f, l;
    do {
        f = k_frames;
        l = *(volatile u32 *)0xdff004 & 0x1ffff;
    } while (f != k_frames);
    /* 256 units per line (313 * 256 per frame, by shifts) */
    return (f << 16) + (f << 14) - (f << 11) + (f << 8) + (l & 0x1ff00) + (l & 0xff);
}
#if V3PROF > 1
#define PB(i) (pt0[i] = v3_now())
#define PE(i) (psec[i] += v3_now() - pt0[i])
#else
#define PB(i) ((void)((i) ? 0 : (pt0[0] = v3_now())))
#define PE(i) ((void)((i) ? 0 : (psec[0] += v3_now() - pt0[0])))
#endif
void v3_prof(int i, int end) { if (end) psec[i] += v3_now() - pt0[i]; else pt0[i] = v3_now(); }
#else
#define wblit k_wait_blitter
#define PB(i) ((void)0)
#define PE(i) ((void)0)
#endif

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
void v3_edge(u8 *row, s32 x, s32 step, int count);
void v3_edge_clip(u8 *row, s32 x, s32 step, int count);
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

/* v * 2^28 / (a^2 + b^2 + c^2) */
static s32 unscale(s32 v, s16 a, s16 b, s16 c)
{
    u32 ss = (u32)muls16(a, a) + (u32)muls16(b, b) + (u32)muls16(c, c), r;
    if ((ss >> 14) == 0)
        return v;
    r = (1ul << 28) / (ss >> 14);           /* 1.0 = 16384 */
    if (r > 16300 && r < 16468)
        return v;                           /* unit length: as it is */
    return (v >> 14) * (s32)r + (((v & 0x3fff) * (s32)r) >> 14);
}

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
        /* the matrix can scale the object's axes (U2A's mother ship by
         * 0.77/0.77/0.88): M^-1 = diag(1 / |column|^2) M^T, so each
         * coordinate is divided by its column's squared length (in
         * 2^28 units); without this, faces seen nearly edge on came out
         * on the wrong side */
        cx = unscale(x, W.m[0], W.m[3], W.m[6]);
        cy = unscale(y, W.m[1], W.m[4], W.m[7]);
        cz = unscale(z, W.m[2], W.m[5], W.m[8]);
    }
}

/* ------------------------------------------------------------ filling */

/* Small polygons: CPU span fill straight into the bitplanes (convex
 * polygons: one span per row from the min/max edge x). */
static s16 spl[256], spr[256];

void v3_span_edge(s16 *spl, s16 *spr, s32 x, s32 step, int count);
void v3_spans(u8 *row, s32 stride, const s16 *spl, const s16 *spr, int count, int color);

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
        int ye, dy, q, r;
        u16 fr;
        s32 X, st;
        xb = xs[j];
        yb = ys[j];
        if (ya > yb) {
            int t = xa; xa = xb; xb = t;
            t = ya; ya = yb; yb = t;
        }
        dy = yb - ya;
        if (dy == 0) {
            if (ya >= y0 && ya < y1) {
                if (xa > xb) { int t = xa; xa = xb; xb = t; }
                if (xa < spl[ya]) spl[ya] = xa;
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
#ifdef __m68k__
        __asm__("divu.w %2,%0" : "=d"(fr) : "0"((u32)r << 16), "d"((u16)dy) : "cc");
#else
        fr = ((u32)r << 16) / dy;
#endif
        st = (s32)((u32)q << 16) + fr;
        X = ((s32)xa << 16) + 0x8000;
        y = ya;
        ye = yb + 1;            /* inclusive: spans include both edge rows */
        if (ye > y1) ye = y1;
        if (y < y0) {
            int k = y0 - y;
            X += (s32)((u32)muls16(q, k) << 16) + mulu16(fr, k);
            y = y0;
        }
        if (ye > y)
            v3_span_edge(spl + y, spr + y, X, st, ye - y);
    }
    v3_spans(scr->planes[0] + muls16(y0, BPR), scr->planes[1] - scr->planes[0],
             spl + y0, spr + y0, y1 - y0, color);
}

/* Blitter queue: the fill, stamp and clear blits of a polygon are queued
 * and started from the blitter interrupt, so the CPU goes on with the
 * next polygons meanwhile. */
#define QN 64
struct bq { u16 con0, con1; u8 *a, *c, *d; s16 amod, cmod, dmod; u16 size; };
static struct bq bq[QN];
static volatile u16 bq_head, bq_tail;
static volatile u32 bq_fin;             /* blits finished */
static volatile u8 bq_busy;
static u32 bq_num;                      /* blits queued */
static u32 clr_seq[2];                  /* the clear blit of each scratch */
static int sc_idx;

static inline void bq_start(const struct bq *e)
{
    BLTCON0 = e->con0;
    BLTCON1 = e->con1;
    BLTAPT = (u32)e->a;
    BLTCPT = (u32)e->c;
    BLTDPT = (u32)e->d;
    BLTAMOD = e->amod;
    BLTCMOD = e->cmod;
    BLTDMOD = e->dmod;
    BLTSIZE = e->size;
}

static void bq_irq(void)
{
    u16 t = bq_tail;
    bq_fin++;
    if (t != bq_head) {
        bq_start(&bq[t]);
        bq_tail = (t + 1) & (QN - 1);
    } else
        bq_busy = 0;
}

/* queue a blit, returns its number */
static u32 bq_add(u16 con0, u16 con1, u8 *a, u8 *c, u8 *d, s16 mod, u16 size)
{
    u16 h = bq_head;
    struct bq *e = &bq[h];
    while (((h + 1) & (QN - 1)) == bq_tail)
        ;
    e->con0 = con0;
    e->con1 = con1;
    e->a = a;
    e->c = c;
    e->d = d;
    e->amod = e->cmod = e->dmod = mod;
    e->size = size;
#if !V3_BQ
    k_wait_blitter();
#ifndef V3_NOBLIT
    bq_start(e);
#endif
    bq_fin = bq_num;            /* all before this one have finished */
#elif defined(__m68k__)
    INTENA = INTF_BLIT;
    if (!bq_busy) {
        bq_busy = 1;
        bq_start(e);
    } else
        bq_head = (h + 1) & (QN - 1);
    INTENA = INTF_SETCLR | INTF_BLIT;
#else
    bq_start(e);
    bq_fin++;
#endif
    return ++bq_num;
}

static void bq_wait(u32 n)
{
#if !V3_BQ
    if (n > bq_fin) {
        k_wait_blitter();
        bq_fin = bq_num;
    }
#endif
    PB(6);
    while (bq_fin < n)
        ;
    PE(6);
}

static void bq_begin(void)
{
    k_wait_blitter();
    BLTAFWM = 0xffff;
    BLTALWM = 0xffff;
    bq_head = bq_tail = 0;
    bq_fin = bq_num = 0;
    bq_busy = 0;
    clr_seq[0] = clr_seq[1] = 0;
#if V3_BQ && defined(__m68k__)
    k_blit_hook = bq_irq;
    INTREQ = INTF_BLIT;
    INTREQ = INTF_BLIT;
    INTENA = INTF_SETCLR | INTF_BLIT;
#endif
}

static void bq_end(void)
{
    bq_wait(bq_num);
#if V3_BQ && defined(__m68k__)
    INTENA = INTF_BLIT;
    k_blit_hook = NULL;
#endif
    k_wait_blitter();
}

void v3_fill(struct v3screen *scr, int n, const s16 *xs, const s16 *ys, int color)
{
#ifdef V3_HOOK_FILL
    V3_HOOK_FILL(n, xs, ys, color);       /* host test harness */
#endif
    int i, ymin = 32767, ymax = -32768, xmin = 32767, xmax = -32768;
    int y0, y1, wx0, wx1, w, p;
    u8 *sc;
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
    if (V3_CPU_FILL && (y1 - y0) * ((xmax >> 4) - (xmin >> 4) + 1) <= 16) {
        /* small: CPU spans (the blitter may still be clearing the scratch
         * plane of the previous polygon, which does not matter here, but
         * it may also still be stamping into the bitplanes) */
        bq_wait(bq_num);
        PB(7);
#ifndef V3_NOSMALL
        fill_cpu(scr, n, xs, ys, color, y0, y1 + 1 > scr->clipy1 ? scr->clipy1 : y1 + 1);
#endif
        PE(7);
        v3_polys_drawn++;
        return;
    }
    /* polygons alternate between two scratch planes: the edges are marked
     * while the blitter still fills and stamps the previous polygon (the
     * plane used now was cleared by the blits waited for before those) */
    sc_idx ^= 1;
    sc = sc_idx ? scr->scratch2 : scr->scratch;
    bq_wait(clr_seq[sc_idx]);
    PB(5);
    /* mark edges: one toggled pixel per row, rows [ytop, ybottom) */
    for (i = 0; i < n; i++) {
        int xa = xs[i], ya = ys[i], xb, yb, j = i + 1 == n ? 0 : i + 1;
        int y, ye, dy, q, r;
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
        y = ya;
        ye = yb;
        if (ye > y1) ye = y1;
        {
            /* 16.16 fixed point in asm (x up to +-16000: fits, the step
             * may wrap around, which the sums survive) */
            u16 fr;
            s32 st, X = ((s32)xa << 16) + 0x8000;
#ifdef __m68k__
            __asm__("divu.w %2,%0" : "=d"(fr) : "0"((u32)r << 16), "d"((u16)dy) : "cc");
#else
            fr = ((u32)r << 16) / dy;
#endif
            st = (s32)((u32)q << 16) + fr;
            if (y < y0) {
                int k = y0 - y;
                X += (s32)((u32)muls16(q, k) << 16) + mulu16(fr, k);
                y = y0;
            }
            if ((u16)xa < 320 && (u16)xb < 320)
                v3_edge(sc + muls16(y, BPR), X, st, ye - y);
            else
                v3_edge_clip(sc + muls16(y, BPR), X, st, ye - y);
        }
    }
    PE(5);
    wx0 = xmin >> 4;
    wx1 = xmax >> 4;
    w = wx1 - wx0 + 1;
    /* blitter fill (descending), stamp into the bitplanes, clear */
    {
        u32 off = y0 * BPR + wx0 * 2;
        u16 size = ((y1 - y0) << 6) | w;
        s16 mod = BPR - 2 * w;
        u8 *last = sc + (y1 - 1) * BPR + wx1 * 2;
        bq_add(0x09f0, 0x000a, last, 0, last, mod, size);      /* IFE | DESC */
        for (p = 0; p < 5; p++) {
            u8 *d = scr->planes[p] + off;
            bq_add(0x0b00 | ((color >> p) & 1 ? 0xfa : 0x0a), 0, sc + off, d, d, mod, size);
        }
        clr_seq[sc_idx] = bq_add(0x0100, 0, 0, 0, sc + off, mod, size);
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
    {
        s32 dx = v[0] - cx, dy = v[1] - cy, dz = v[2] - cz;
        if ((u32)(dx + 32768) < 65536 && (u32)(dy + 32768) < 65536 && (u32)(dz + 32768) < 65536)
            d = mul16(nn[0], dx) + mul16(nn[1], dy) + mul16(nn[2], dz);   /* exact */
        else
            d = mul32x16(dx, nn[0]) + mul32x16(dy, nn[1]) + mul32x16(dz, nn[2]);
    }
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
    PB(4);
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
        if (n < 3) {
            PE(4);
            return;
        }
#ifdef V3_DEBUG_CLIP
        V3_DEBUG_CLIP(o, pd, n, xs, ys);
#endif
    }
    {
        int col = (pd[1] + shade_of((flags & 0x10) ? (flags | 0x0c) : flags, nn)) & 0xff;
        PE(4);
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
    PB(2);
    v3_xform_idx(o->v, vlist, nv, &xf, vo);
    PE(2);
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
    PB(3);
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
    PE(3);
    PB(2);
    v3_xform_idx(o->v, vlist, nv, &xf, vo);
    PE(2);
    for (i = 0; i < nvis; i++)
        draw_poly(s, o, vis[i]);
}

void v3_draw_(struct v3scene *s, struct v3screen *scr);
void v3_draw(struct v3scene *s, struct v3screen *scr)
{
#ifdef V3PROF
    {
        static u32 last;
        u32 t = v3_now();
        if (last) psec[8] += t - last;
        last = t;
    }
    PB(0);
    v3_draw_(s, scr);
    k_wait_blitter();
    PE(0);
    {
        /* overlay (benchmark: the part steps the animation by a fixed
         * amount per draw): totals of the first 150 draws in 65536 colour
         * clock units, shown alternately:
         *   tag << 28 | a << 14 | b: 1 interval, draw  2 transform, cull
         *   3 edges, blitter waits  4 object setup, polygon setup  5 draws */
        extern volatile u32 k_dbg_val, k_dbg_phase;
        static u32 tot[9], n;
        int i;
        if (n < 150) {
            n++;
            for (i = 0; i < 9; i++) tot[i] += psec[i];
        }
        memset(psec, 0, sizeof(psec));
#define TV(i) ((tot[i] >> 16) & 0x3fff)
        {
            static const u8 pairs[5][2] = { { 8, 0 }, { 2, 3 }, { 5, 6 }, { 1, 4 }, { 7, 7 } };
            int d = (k_frames / 50) % 5;
            k_dbg_val = (u32)(d + 1) << 28 | (u32)TV(pairs[d][0]) << 14 | TV(pairs[d][1]);
            if (d == 4) k_dbg_val = 0x50000000 | (u32)TV(7) << 14 | n;
        }
    }
#else
    v3_draw_(s, scr);
#endif
}

void v3_draw_(struct v3scene *s, struct v3screen *scr)
{
    static u8 order[V3_MAXINST];
    static s32 dist[V3_MAXINST];
    static struct mat mats[V3_MAXINST];
    int n = 0, a, b;
    PB(1);
    S = scr;
    bq_begin();
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
    PE(1);
    for (a = 0; a < n; a++) {
        int k = order[a];
        W = mats[k];
        draw_object(s, &s->objs[s->inst[k]]);
    }
    bq_end();
}
