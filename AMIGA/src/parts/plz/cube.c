/*
 * PLZPART second half: the plasma textured cube (VECT.C, PLZFILL.C,
 * SPLINE.ASM).  The camera and light follow the original spline; the
 * faces are texture mapped with the waving sine texture.
 *
 * Amiga version: 80x66 chunky cells of 4x3 pixels (the original renders
 * 134 rows of 4 pixel wide texels), 5 bitplanes: 10 shades for each of the
 * three face colours.  Only the cube's bounding box is converted.
 */
#include "kernel.h"
#include "cube_data.h"

#define LINE0 0x48
#define CROWS 66
#define CPL (40 * CROWS)
#define CTOP (LINE0 + 1)

void cube_span(u8 *dst, int n, u32 u, u32 v, u32 du, s32 dv,
               const u8 *tex, const u16 *sh, int add);
void cube_c2p(const u8 *chunky, u8 *tmp, u8 *p4, int ngroups, int nrows,
              const u8 *lt, const u8 *bt);

static const s16 pts[8][3] = {
    {125, 125, 125}, {125, -125, 125}, {-125, -125, 125}, {-125, 125, 125},
    {125, 125, -125}, {125, -125, -125}, {-125, -125, -125}, {-125, 125, -125}};
static const u8 faces[6][5] = {
    {1, 2, 3, 0, 0}, {7, 6, 5, 4, 0}, {0, 4, 5, 1, 1},
    {1, 5, 6, 2, 2}, {2, 6, 7, 3, 1}, {3, 7, 4, 0, 2}};
static const s16 txt[4][2] = {{64, 4}, {190, 4}, {190, 60}, {64, 60}};

static s32 kx, ky, kz, dis, tx, ty, ls_kx, ls_ky;
static s32 rx[8], ry[8], rz[8];
static s16 sx[8], sy[8];
static u8 *chunky, *tmp[2], *p4buf, *scr[2];
static u16 *shtab;
static u32 *cop[2];
static u16 *colp[2];
static u32 ltab[64];
static u8 btab[256];
static int lights[3] = {-1, -1, -1};

static inline s32 divs16(s32 a, s16 b)
{
    __asm__("divs.w %1,%0" : "+d"(a) : "dmi"(b) : "cc");
    return (s16)a;
}

#define SIN(a) ((s32)sinit[(a) & 1023])
#define COS(a) ((s32)sinit[((a) & 1023) + 256])

static void getspl(int pos)
{
    const s16 *r = rata + (pos >> 8) * 8;
    int t = pos & 255, i;
    s32 v[8];
    for (i = 0; i < 8; i++) {
        s32 s = muls16(r[24 + i], splcoef[t]) + muls16(r[16 + i], splcoef[t + 256]) +
                muls16(r[8 + i], splcoef[t + 512]) + muls16(r[i], splcoef[t + 768]);
        v[i] = (s16)(s >> 15);
    }
    tx = v[0]; ty = v[1]; dis = v[2];
    kx = v[3]; ky = v[4]; kz = v[5];
    ls_kx = v[6]; ls_ky = v[7];
}

static void rotate(void)
{
    s32 SX = SIN(kx), SY = SIN(ky), SZ = SIN(kz), CX = COS(kx), CY = COS(ky), CZ = COS(kz);
    s32 cxx = (CY * CZ) >> 22, cxy = (CY * SZ) >> 22, cxz = -SY >> 7;
    s32 cyx = ((((SX * CZ + 16384) >> 15) * SY) - CX * SZ) >> 22;
    s32 cyy = ((((SX * SY + 16384) >> 15) * SZ) + CX * CZ) >> 22;
    s32 cyz = (CY * SX) >> 22;
    s32 czx = ((((CX * CZ + 16384) >> 15) * SY) + SX * SZ) >> 22;
    s32 czy = ((((CX * SY + 16384) >> 15) * SZ) - SX * CZ) >> 22;
    s32 czz = (CY * CX) >> 22;
    int a;
    for (a = 0; a < 8; a++) {
        s32 x = pts[a][0], y = pts[a][1], z = pts[a][2];
        rx[a] = ((((x * cxx) >> 1) + ((y * cxy) >> 1) + ((z * cxz) >> 1)) >> 7) + tx;
        ry[a] = ((((x * cyx) >> 1) + ((y * cyy) >> 1) + ((z * cyz) >> 1)) >> 7) + ty;
        rz[a] = ((((x * czx) >> 1) + ((y * czy) >> 1) + ((z * czz) >> 1)) >> 7) + dis;
        if (rz[a] < 16) rz[a] = 16;
        sx[a] = rx[a] * 256 / rz[a] + 320;
        sy[a] = ry[a] * 142 / rz[a] + 66;
    }
}

/* face colours: index 1 + 10 * colour + level */
static void shade(int c, int light)
{
    int l, b;
    if (lights[c] == light) return;
    lights[c] = light;
    for (l = 0; l < NLEV; l++) {
        const u8 *p = cpal + (c * 64 + klev[l]) * 3;
        u16 v = (((p[0] * light >> 6) >> 2) << 8) | (((p[1] * light >> 6) >> 2) << 4) |
                ((p[2] * light >> 6) >> 2);
        for (b = 0; b < 2; b++)
            colp[b][(1 + c * NLEV + l) * 2 + 1] = v;
    }
}

/* bounding box in cells / rows of what was drawn */
static int bx0, bx1, by0, by1;

static void poly(const int *idx, int col)
{
    int n, s1, s2, d1, d2, y, ym, i, top = 0;
    s32 x1, x2, ax1, ax2, u1, v1, u2, v2, au1, av1, au2, av2;
    int px[4], py[4];
    for (i = 0; i < 4; i++) {
        px[i] = sx[idx[i]];
        py[i] = sy[idx[i]];
        if (py[i] < py[top]) top = i;
    }
#define EDGE(s, d, X, AX, U, AU, V, AV) do { \
        int dy = py[d] - py[s]; if (dy == 0) dy = 1; \
        X = ((s32)px[s] << 16) + 0x8000; AX = (((s32)(px[d] - px[s])) << 16) / dy; \
        U = (s32)txt[s][0] << 16; AU = (((s32)(txt[d][0] - txt[s][0])) << 16) / dy; \
        V = (s32)txt[s][1] << 16; AV = (((s32)(txt[d][1] - txt[s][1])) << 16) / dy; \
    } while (0)
    s1 = s2 = top;
    d1 = (s1 + 1) & 3;
    d2 = (s2 - 1) & 3;
    EDGE(s1, d1, x1, ax1, u1, au1, v1, av1);
    EDGE(s2, d2, x2, ax2, u2, au2, v2, av2);
    y = py[top];
    for (n = 0; n < 4;) {
        ym = py[d1] < py[d2] ? py[d1] : py[d2];
        for (; y < ym; y++) {
            if (!(y & 1) && y >= 0 && y < CROWS * 2) {
                /* left/right edge in cells (640 wide virtual -> 80 cells) */
                s32 xa = x1, xb = x2, ua = u1, ub = u2, va = v1, vb = v2;
                int c0, c1, w, cl;
                s32 du, dv, uu, vv;
                if (xa > xb) {
                    s32 t;
                    t = xa; xa = xb; xb = t;
                    t = ua; ua = ub; ub = t;
                    t = va; va = vb; vb = t;
                }
                c0 = ((xa >> 16) - 160 + 2) >> 2;
                c1 = ((xb >> 16) - 160 + 2) >> 2;
                w = c1 - c0;
                if (w > 0) {
                    du = divs16((ub - ua) >> 8, w);     /* 8.8 per cell */
                    dv = divs16((vb - va) >> 8, w);
                    uu = ua >> 8;
                    vv = va >> 8;
                    cl = c0;
                    if (cl < 0) { uu -= du * cl; vv -= dv * cl; cl = 0; }
                    if (c1 > 80) c1 = 80;
                    if (c1 > cl) {
                        int r = y >> 1;
                        cube_span(chunky + r * 80 + cl, c1 - cl,
                                  ((u32)(uu & 255) << 24) | (u16)(uu >> 8), vv,
                                  ((u32)(du & 255) << 24) | (u16)(du >> 8), dv,
                                  ktex, shtab, (1 + col * NLEV) * 4);
                        if (cl < bx0) bx0 = cl;
                        if (c1 > bx1) bx1 = c1;
                        if (r < by0) by0 = r;
                        if (r + 1 > by1) by1 = r + 1;
                    }
                }
            }
            x1 += ax1; u1 += au1; v1 += av1;
            x2 += ax2; u2 += au2; v2 += av2;
        }
        if (py[d1] == py[d2]) {
            s1 = d1; d1 = (s1 + 1) & 3;
            s2 = d2; d2 = (s2 - 1) & 3;
            n += 2;
            EDGE(s1, d1, x1, ax1, u1, au1, v1, av1);
            EDGE(s2, d2, x2, ax2, u2, au2, v2, av2);
        } else if (py[d1] < py[d2]) {
            s1 = d1; d1 = (s1 + 1) & 3; n++;
            EDGE(s1, d1, x1, ax1, u1, au1, v1, av1);
        } else {
            s2 = d2; d2 = (s2 - 1) & 3; n++;
            EDGE(s2, d2, x2, ax2, u2, au2, v2, av2);
        }
    }
}

static void draw_faces(void)
{
    s32 ls_x, ls_y, ls_z;
    int f;
    ls_y = COS(ls_kx) >> 8;
    ls_x = ((SIN(ls_kx) >> 8) * (SIN(ls_ky) >> 8)) >> 7;
    ls_z = ((SIN(ls_kx) >> 8) * (COS(ls_ky) >> 8)) >> 7;
    for (f = 0; f < 6; f++) {
        int p1 = faces[f][0], p2 = faces[f][1], p3 = faces[f][2], i;
        s32 ax = rx[p2] - rx[p1], ay = ry[p2] - ry[p1], az = rz[p2] - rz[p1];
        s32 bx = rx[p3] - rx[p1], by = ry[p3] - ry[p1], bz = rz[p3] - rz[p1];
        s32 nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
        s32 s = -rx[p1] * nx - ry[p1] * ny - rz[p1] * nz;
        int idx[4];
        if (s > 0) continue;
        s = (ls_x * nx + ls_y * ny + ls_z * nz) / 250000 + 32;
        if (s < 0) s = 0;
        if (s > 64) s = 64;
        shade(faces[f][4], s);
        for (i = 0; i < 4; i++)
            idx[i] = faces[f][i];
        poly(idx, faces[f][4]);
    }
}

void cube_main(void)
{
    u32 *c;
    int i, b = 0, r, j, p;
    int ob[3][4];                        /* bounding boxes of the last frames */
    u32 t0;
    chunky = k_alloc(80 * CROWS);
    p4buf = k_alloc(40 * CROWS);
    shtab = k_alloc(128 * 256);
    for (i = 0; i < 2; i++) {
        scr[i] = k_alloc(CPL * 5);
        tmp[i] = k_alloc(160 * CROWS);
        cop[i] = k_alloc((64 + CROWS * 9) * 4);
        c = cop[i];
        *c++ = CMOVE(R_DIWSTRT, 0x2c81);
        *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
        *c++ = CMOVE(R_DDFSTRT, 0x0038);
        *c++ = CMOVE(R_DDFSTOP, 0x00d0);
        *c++ = CMOVE(R_BPL1MOD, 0);
        *c++ = CMOVE(R_BPL2MOD, 0);
        *c++ = CMOVE(R_BPLCON1, 0);
        *c++ = CMOVE(R_BPLCON2, 0);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        colp[i] = (u16 *)c;
        for (j = 0; j < 32; j++)
            *c++ = CMOVE(R_COLOR00 + j * 2, 0);
        for (j = 0; j < 5; j++) {
            u32 a = (u32)scr[i] + j * CPL;
            *c++ = CMOVE(R_BPL1PTH + j * 4, a >> 16);
            *c++ = CMOVE(R_BPL1PTH + j * 4 + 2, a & 0xffff);
        }
        *c++ = CWAIT(CTOP, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x5200);
        for (r = 0; r < CROWS * 3; r++) {
            int l = CTOP + r, m = (r % 3) == 2 ? 0 : -40;
            if (l == 256) *c++ = CWAIT_PAL;
            *c++ = CWAIT(l & 0xff, 0x07);
            *c++ = CMOVE(R_BPL1MOD, m);
            *c++ = CMOVE(R_BPL2MOD, m);
        }
        *c++ = CWAIT((CTOP + CROWS * 3) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
    }
    /* C2P tables: two cells per plane byte */
    for (p = 0; p < 2; p++)
        for (j = 0; j < 32; j++) {
            u32 l = 0;
            u8 m = p ? 0x0f : 0xf0;
            for (i = 0; i < 4; i++)
                if (j & (1 << i)) l |= (u32)m << (24 - 8 * i);
            ltab[p * 32 + j] = l;
            btab[p * 128 + j * 4] = (j & 16) ? m : 0;
        }
    for (i = 0; i < 3; i++)
        ob[i][0] = 80, ob[i][1] = 0, ob[i][2] = CROWS, ob[i][3] = 0;
    k_set_copper(cop[0]);
    while (!k_exit() && k_mus_plus() < 13)
        k_waitvbl();
    t0 = k_frames;
    for (;;) {
        int frames = (k_frames - t0) * 7 / 5, a, g0, g1, r0, r1, dd;
        int pos = 4 * 256 + frames * 4;
        a = k_mus_plus();
        if (k_exit() || (a >= -4 && a < 0)) break;
        if ((pos >> 8) + 3 >= RATAN) pos = (RATAN - 4) * 256 + 255;
        getspl(pos);
        rotate();
        /* wave offsets of the texture rows for this frame */
        dd = ((frames & 63) + 1);
        for (i = 0; i < 64; i++)
            shtab[i * 128] = kdist[(i + dd) & 127];
        /* clear what was drawn last frame */
        for (r = ob[0][2]; r < ob[0][3]; r++)
            memset(chunky + r * 80 + ob[0][0], 0, ob[0][1] - ob[0][0]);
        bx0 = 80; bx1 = 0; by0 = CROWS; by1 = 0;
        draw_faces();
        /* convert the union of this and the last two boxes (so the old
         * pictures on both screens get cleared too) */
        g0 = bx0; g1 = bx1; r0 = by0; r1 = by1;
        for (i = 0; i < 2; i++) {
            if (ob[i][0] < g0) g0 = ob[i][0];
            if (ob[i][1] > g1) g1 = ob[i][1];
            if (ob[i][2] < r0) r0 = ob[i][2];
            if (ob[i][3] > r1) r1 = ob[i][3];
        }
        ob[2][0] = ob[1][0]; ob[2][1] = ob[1][1]; ob[2][2] = ob[1][2]; ob[2][3] = ob[1][3];
        ob[1][0] = ob[0][0]; ob[1][1] = ob[0][1]; ob[1][2] = ob[0][2]; ob[1][3] = ob[0][3];
        ob[0][0] = bx0; ob[0][1] = bx1; ob[0][2] = by0; ob[0][3] = by1;
        g0 >>= 2;
        g1 = (g1 + 3) >> 2;
        k_wait_blitter();
        if (g1 > g0 && r1 > r0) {
            cube_c2p(chunky + r0 * 80 + g0 * 4, tmp[b] + r0 * 160 + g0 * 8, p4buf + r0 * 40 + g0 * 2,
                     g1 - g0, r1 - r0, (const u8 *)ltab + 128, btab + 128);
            for (p = 0; p < 4; p++) {
                int rr = r0;
                while (rr < r1) {
                    int n = r1 - rr > 48 ? 48 : r1 - rr;
                    k_wait_blitter();
                    BLTCON0 = 0x09f0;
                    BLTCON1 = 0;
                    BLTAFWM = 0xffff;
                    BLTALWM = 0xffff;
                    BLTAPT = (u32)(tmp[b] + rr * 160 + p * 2);
                    BLTDPT = (u32)(scr[b] + p * CPL + rr * 40);
                    BLTAMOD = 6;
                    BLTDMOD = 0;
                    BLTSIZE = ((n * 20) << 6) | 1;
                    rr += n;
                }
            }
            k_wait_blitter();
            BLTCON0 = 0x09f0;
            BLTAPT = (u32)(p4buf + r0 * 40);
            BLTDPT = (u32)(scr[b] + 4 * CPL + r0 * 40);
            BLTAMOD = 0;
            BLTDMOD = 0;
            BLTSIZE = ((r1 - r0) << 6) | 20;
        }
        k_wait_blitter();
        k_set_copper(cop[b]);
        k_waitvbl();
        b ^= 1;
    }
    for (i = 0; i < 32; i++)
        colp[0][i * 2 + 1] = colp[1][i * 2 + 1] = 0;
    k_waitvbl();
}
