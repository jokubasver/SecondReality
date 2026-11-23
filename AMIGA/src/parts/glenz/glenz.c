/*
 * GLENZ - translucent "glenz" vectors on a bouncing checkered plate.
 * Original: GLENZ/MAIN.C, ZOOMER.C, VEC.ASM (Psi).
 *
 *  1. zoomer2: the title picture is closed by bars from top and bottom
 *     while fading to grey (done with the copper over BEGLOGO's picture).
 *  2. the plate drops in and bounces.
 *  3. glenz object A bounces on the plate (jello), later object B joins.
 *
 * Bitplanes: P0,P1 plate (later object B), P2 back faces (colour bit 2),
 * P3,P4 front face shade slot.  Every plane is drawn by toggling edge
 * pixels and filling with the blitter.  The 32 colours are recomputed each
 * frame like the original's additive palette.
 */
#include "kernel.h"
#include "../../common/xorfill.h"
#include "glenz_data.h"

#define W 320
#define H 200
#define BPR 40
#define PL (BPR * H)
#define LINE0 0x48

static u8 *buf[2];
static u8 *scratch;
static u32 *cop[2];
static u16 *colp[2];
static int cur;

/* ---------------------------------------------------------------- data */
static const s16 pts_a[14 * 3] = {
    -5000,-5000,-5000, 5000,-5000,-5000, 5000,5000,-5000, -5000,5000,-5000,
    -5000,-5000,5000, 5000,-5000,5000, 5000,5000,5000, -5000,5000,5000,
    0,0,-8500, 0,0,8500, 8500,0,0, -8500,0,0, 0,8500,0, 0,-8500,0 };
#define QQ 99
static const s16 pts_b[14 * 3] = {
    -60*QQ,-60*QQ,-60*QQ, 60*QQ,-60*QQ,-60*QQ, 60*QQ,60*QQ,-60*QQ, -60*QQ,60*QQ,-60*QQ,
    -60*QQ,-60*QQ,60*QQ, 60*QQ,-60*QQ,60*QQ, 60*QQ,60*QQ,60*QQ, -60*QQ,60*QQ,60*QQ,
    0,0,-105*QQ, 0,0,105*QQ, 105*QQ,0,0, -105*QQ,0,0, 0,105*QQ,0, 0,-105*QQ,0 };
/* triangles: colour, v0, v1, v2 */
static const u8 tri_a[24 * 4] = {
    0x02,0,1,8, 0x04,1,2,8, 0x06,2,3,8, 0x08,3,0,8,
    0x0a,2,1,10, 0x0c,1,5,10, 0x0e,5,6,10, 0x10,6,2,10,
    0x12,2,6,12, 0x14,6,7,12, 0x16,7,3,12, 0x18,3,2,12,
    0x1a,0,3,11, 0x1c,3,7,11, 0x1e,7,4,11, 0x20,4,0,11,
    0x22,5,1,13, 0x24,1,0,13, 0x26,0,4,13, 0x28,4,5,13,
    0x2a,5,4,9, 0x2c,4,7,9, 0x2e,7,6,9, 0x30,6,5,9 };
static const u8 tri_b[24 * 4] = {
    0x04,0,1,8, 0x02,1,2,8, 0x04,2,3,8, 0x02,3,0,8,
    0x04,2,1,10, 0x02,1,5,10, 0x04,5,6,10, 0x02,6,2,10,
    0x04,2,6,12, 0x02,6,7,12, 0x04,7,3,12, 0x02,3,2,12,
    0x04,0,3,11, 0x02,3,7,11, 0x04,7,4,11, 0x02,4,0,11,
    0x04,5,1,13, 0x02,1,0,13, 0x04,0,4,13, 0x02,4,5,13,
    0x04,5,4,9, 0x02,4,7,9, 0x04,7,6,9, 0x02,6,5,9 };

/* sin with 3600 steps per circle, unit 32768 (the original's sintable16) */
static s16 sin36(int a)
{
    a %= 3600;
    if (a < 0) a += 3600;
    return k_sin(a * 1024 / 3600) * 2;
}
static s16 cos36(int a) { return sin36(a + 900); }
static int sin255(int a) { return muls16(k_sin(a), 255) >> 14; }

/* rY*rX*rZ as in GLENZ/MATH.ASM, unit 32768 */
static void matrix_yxz(int rx, int ry, int rz, s16 *m)
{
    s16 xs = sin36(ry), xc = cos36(ry);   /* note: original swaps x/y args */
    s16 ys = sin36(rx), yc = cos36(rx);
    s16 zs = sin36(rz), zc = cos36(rz);
#define M(a, b) ((s16)(muls16((a), (b)) >> 15))
    m[14] = M(ys, zs);
    m[0] = M(yc, zc);
    m[14] -= M(m[0], xs);
    {
        s16 t = M(xs, ys);
        m[0] -= M(zs, t);
        m[2] = M(zc, t);
    }
    {
        s16 t = M(yc, zs);
        m[2] += t;
        m[12] = M(xs, t);
    }
    m[12] += M(ys, zc);
    m[6] = -M(xc, zs);
    m[8] = M(xc, zc);
    m[4] = -M(xc, ys);
    m[16] = M(xc, yc);
    m[10] = xs;
#undef M
}

/* ---------------------------------------------------------------- 3D */
static s16 sx[14], sy[14];

static void transform(const s16 *pts, const s16 *mw, int scale_x, int scale_y, int scale_z,
                      s32 tx, s32 ty, s32 tz, int clipy)
{
    /* the matrix m is stored like the original: words at even indices */
    int i;
    for (i = 0; i < 14; i++) {
        const s16 *p = pts + i * 3;
        s32 x = (muls16(mw[0], p[0]) + muls16(mw[2], p[1]) + muls16(mw[4], p[2])) >> 15;
        s32 y = (muls16(mw[6], p[0]) + muls16(mw[8], p[1]) + muls16(mw[10], p[2])) >> 15;
        s32 z = (muls16(mw[12], p[0]) + muls16(mw[14], p[1]) + muls16(mw[16], p[2])) >> 15;
        x = (muls16(x, scale_x * 64) >> 15) + tx;
        y = (muls16(y, scale_y * 64) >> 15) + ty;
        z = (muls16(z, scale_z * 64) >> 15) + tz;
        if (clipy && y > 1500) y = 1500;
        if (z < 128) z = 128;
        {
            s32 qx = x * 256, qy = y * 213;
            s16 q;
            __asm__("divs.w %2,%0" : "=d"(q) : "0"(qx), "d"((s16)z) : "cc");
            sx[i] = q + 160;
            __asm__("divs.w %2,%0" : "=d"(q) : "0"(qy), "d"((s16)z) : "cc");
            sy[i] = q + 130;
        }
    }
}

static int bbx0, bbx1, bby0, bby1;
/* what was drawn into each buffer (and the scratch plane) last time: only
 * that area is cleared (words x, width, rows y, height; height < 0: all) */
static s16 lbx[2], lbw[2], lby[2], lbh[2] = { -1, -1 }, lcp[2];
static s16 sbx, sbw = BPR / 2, sby, sbh = H;

static void bbox_points(void)
{
    int i;
    for (i = 0; i < 14; i++) {
        if (sx[i] < bbx0) bbx0 = sx[i];
        if (sx[i] > bbx1) bbx1 = sx[i];
        if (sy[i] < bby0) bby0 = sy[i];
        if (sy[i] > bby1) bby1 = sy[i];
    }
}

static void tri_edges(u8 *plane, const u8 *t)
{
    int a = t[1], b = t[2], c = t[3];
    xf_edge(plane, BPR, W, sx[a], sy[a], sx[b], sy[b], 0, H);
    xf_edge(plane, BPR, W, sx[b], sy[b], sx[c], sy[c], 0, H);
    xf_edge(plane, BPR, W, sx[c], sy[c], sx[a], sy[a], 0, H);
}

/* cross product of the projected triangle (checkhiddenbx); >=0 visible */
static s32 tri_area(const u8 *t)
{
    int a = t[1], b = t[2], c = t[3];
    return muls16(sx[a] - sx[b], sy[a] - sy[c]) - muls16(sy[a] - sy[b], sx[a] - sx[c]);
}

/* front face shade slots for object A */
static u8 slot_rgb[4][3];
static int lightshift = 9;

static void draw_a(u8 *b)
{
    int i, n = 0;
    s16 key[24];
    u8 face[24], rgb[24][3];
    for (i = 0; i < 24; i++) {
        const u8 *t = tri_a + i * 4;
        s32 area = tri_area(t);
        if (area < 0) {
            /* back face: colour bit 2 when the face colour has bit 1 */
            if (t[0] & 2)
                tri_edges(b + 2 * PL, t);
        } else {
            int l;
            if (lightshift == 9) l = area >> 7;
            else l = (area >> 8) + (area >> 9);
            if (l < 0) l = 0;
            if (l > 63) l = 63;
            if (t[0] & 2) { rgb[n][0] = 7; rgb[n][1] = l >> 1; rgb[n][2] = l; }
            else { rgb[n][0] = rgb[n][1] = rgb[n][2] = l; }
            key[n] = l + ((t[0] & 2) ? 64 : 0);
            face[n] = i;
            n++;
        }
    }
    /* group the visible faces into 3 slots by brightness/tint */
    {
        int j, k, order[24];
        for (i = 0; i < n; i++) order[i] = i;
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && key[order[j - 1]] > key[order[j]]; j--) {
                int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        for (k = 1; k <= 3; k++) {
            int s0 = (k - 1) * n / 3, s1 = k * n / 3, r = 0, g = 0, bl = 0, c = s1 - s0;
            for (j = s0; j < s1; j++) {
                int f = order[j];
                r += rgb[f][0]; g += rgb[f][1]; bl += rgb[f][2];
                {
                    const u8 *t = tri_a + face[f] * 4;
                    if (k & 1) tri_edges(b + 3 * PL, t);
                    if (k & 2) tri_edges(b + 4 * PL, t);
                }
            }
            if (c) {
                slot_rgb[k][0] = r / c;
                slot_rgb[k][1] = g / c;
                slot_rgb[k][2] = bl / c;
            }
        }
    }
}

static void draw_b(u8 *b, int *used_scratch)
{
    int i;
    for (i = 0; i < 24; i++) {
        const u8 *t = tri_b + i * 4;
        if (tri_area(t) >= 0) {
            if (t[0] & 2)
                tri_edges(b + 0 * PL, t);
        } else {
            if (t[0] == 2)
                tri_edges(b + 1 * PL, t);
            else {
                tri_edges(scratch, t);
                *used_scratch = 1;
            }
        }
    }
}

/* ---------------------------------------------------------------- palette */
static u8 base[8][3];            /* colours of indices 0..7 (unfaded) */
static int base_fade = 64;

static void make_palette(int b)
{
    int i;
    u16 *cp = colp[b];
    for (i = 0; i < 32; i++) {
        int k = i & 7, s = i >> 3, r, g, bl;
        if (!s) {
            r = base[k][0] * base_fade >> 6;
            g = base[k][1] * base_fade >> 6;
            bl = base[k][2] * base_fade >> 6;
        } else {
            r = slot_rgb[s][0] + (base[k][0] >> 2);
            g = slot_rgb[s][1] + (base[k][1] >> 2);
            bl = slot_rgb[s][2] + (base[k][2] >> 2);
        }
        if (r > 63) r = 63;
        if (g > 63) g = 63;
        if (bl > 63) bl = 63;
        cp[i * 2 + 1] = ((r >> 2) << 8) | ((g >> 2) << 4) | (bl >> 2);
    }
}

static void build_copper(int b, int planes)
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
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    colp[b] = (u16 *)c;
    for (i = 0; i < 32; i++)
        *c++ = CMOVE(R_COLOR00 + i * 2, 0);
    for (i = 0; i < 5; i++) {
        u32 a = (u32)buf[b] + i * PL;
        *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
        *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
    }
    *c++ = CWAIT(LINE0, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200 | (planes << 12));
    *c++ = CWAIT_PAL;
    *c++ = CWAIT((LINE0 + H) & 0xff, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CMOVE(R_COLOR00, 0);
    *c++ = CEND;
}

/* ---------------------------------------------------------------- phases */
static u32 t70_0;
static int t70(void) { return (k_frames - t70_0) * 7 / 5; }

/* 1. close the title picture with grey bars (ZOOMER.C zoomer2) */
static void zoomer(void)
{
    const u8 *m = k_msg[1];
    const u8 *pic = (const u8 *)*(const u32 *)m;
    const u16 *tcol = (const u16 *)(m + 4);
    u32 *c0 = k_alloc(160 * 4), *c;
    int zy = 0, zya = 0, frame = 0, steps = 0, i;
    t70_0 = k_frames;
    while (zy < 260 && !k_exit()) {
        int target = t70();
        while (steps < target && zy < 260) {
            zya++;
            zy += zya / 4;
            if (zy > 260) zy = 260;
            frame++;
            steps++;
        }
        {
            int f = frame > 32 ? 32 : frame, top = zy / 2, bot = (125 * zy / 260) / 2;
            u16 grey = ((30 * 15 / 63) << 8) | ((30 * 15 / 63) << 4) | (30 * 15 / 63);
            c = c0;
            *c++ = CMOVE(R_DIWSTRT, 0x2c81);
            *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
            *c++ = CMOVE(R_DDFSTRT, 0x0038);
            *c++ = CMOVE(R_DDFSTOP, 0x00d0);
            *c++ = CMOVE(R_BPL1MOD, 0);
            *c++ = CMOVE(R_BPL2MOD, 0);
            *c++ = CMOVE(R_BPLCON0, 0x0200);
            for (i = 0; i < 32; i++) {
                u16 tc = tcol[i];
                int r = (((tc >> 8) & 15) * (32 - f) + (30 * 15 / 63) * f) >> 5;
                int g = (((tc >> 4) & 15) * (32 - f) + (30 * 15 / 63) * f) >> 5;
                int b = ((tc & 15) * (32 - f) + (30 * 15 / 63) * f) >> 5;
                *c++ = CMOVE(R_COLOR00 + i * 2, (r << 8) | (g << 4) | b);
            }
            for (i = 0; i < 5; i++) {
                u32 a = (u32)pic + i * PL + top * BPR;
                *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
                *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
            }
            /* top bar: grey up to line 'top', bottom bar from H-bot */
            *c++ = CMOVE(R_COLOR00, grey);
            *c++ = CWAIT(LINE0 + top, 0x07);
            if (top < H - bot) {
                *c++ = CMOVE(R_BPLCON0, 0x5200);
                if (LINE0 + H - bot > 255) *c++ = CWAIT_PAL;
                *c++ = CWAIT((LINE0 + H - bot) & 0xff, 0x07);
                *c++ = CMOVE(R_BPLCON0, 0x0200);
                *c++ = CMOVE(R_COLOR00, grey);
            }
            *c++ = CEND;
            k_set_copper(c0);
        }
        k_waitvbl();
    }
    /* black */
    k_set_copper(k_empty_copper);
    k_waitvbl();
    k_release_old();
}

/* 2. the plate drops and bounces: rows of the plate scaled vertically */
static void draw_plate(u8 *b, int y)
{
    int y1 = 130 + y / 2, y2 = 130 + y * 3 / 2, ry, c = 0, bstep;
    u8 *p0 = b, *p1 = b + PL;
    memset(p0, 0, 2 * PL);
    bstep = y2 != y1 ? 25600 / (y2 - y1) : 0;
    for (ry = y1; ry < y2; ry++, c += bstep) {
        if (ry < 0 || ry >= H) continue;
        memcpy(p0 + ry * BPR, plate0 + (c >> 8) * BPR, BPR);
        memcpy(p1 + ry * BPR, plate1 + (c >> 8) * BPR, BPR);
    }
    for (c = 0; c < 8; c++, ry++) {
        if (ry < 0 || ry >= H) continue;
        memcpy(p0 + ry * BPR, plate0 + (100 + c) * BPR, BPR);
        memcpy(p1 + ry * BPR, plate1 + (100 + c) * BPR, BPR);
    }
}

static void plate_bounce(void)
{
    int yy = 0, ya = 0, steps = 0, done = 0, i;
    for (i = 0; i < 8; i++) {
        base[i][0] = plate_pal[(i & 3) * 3];
        base[i][1] = plate_pal[(i & 3) * 3 + 1];
        base[i][2] = plate_pal[(i & 3) * 3 + 2];
    }
    base_fade = 64;
    t70_0 = k_frames;
    while (!done && !k_exit()) {
        int target = t70();
        while (steps < target && !done) {
            ya++;
            yy += ya;
            if (yy > 48 * 16) {
                yy -= ya;
                ya = -ya * 2 / 3;
                if (ya > -4 && ya < 4) done = 1;
            }
            steps++;
        }
        cur ^= 1;
        draw_plate(buf[cur], yy / 16);
        make_palette(cur);
        k_set_copper(cop[cur]);
        k_waitvbl();
    }
    /* the final plate position stays in both buffers */
    draw_plate(buf[cur ^ 1], yy / 16);
}

void part_main(void)
{
    int rx = 0, ry = 0, rz = 0, zpos = 7500, ypos = -9000, yposa = 0;
    int boingm = 6, boingd = 7, jello = 0, jelloa = 0;
    int xscale = 120, yscale = 120, zscale = 120, bscale = 0;
    int oxp = 0, oyp = 0, ozp = 0, oxb = 0, oyb = 0, ozb = 0;
    int frame = 0, a, b, i;
    u32 last;
    int done70 = 0;
    s16 m[18];
    int clear_planes = 3;

    k_hold_old();
    buf[0] = k_alloc(PL * 5);
    buf[1] = k_alloc(PL * 5);
    scratch = k_alloc(PL);
    cop[0] = k_alloc(80 * 4);
    cop[1] = k_alloc(80 * 4);
    build_copper(0, 5);
    build_copper(1, 5);

    while (!k_exit() && k_mus_plus() < -19 && !k_mus_after(2, 45))
        k_waitvbl();
    k_mus_frame = 0;
    zoomer();
    plate_bounce();
    while (!k_exit() && k_mus_frame < 333)
        k_waitvbl();

    for (i = 0; i < 8; i++) {
        const u8 *src = (i & 4) ? plate_back : plate_pal;
        base[i][0] = src[(i & 3) * 3];
        base[i][1] = src[(i & 3) * 3 + 1];
        base[i][2] = src[(i & 3) * 3 + 2];
    }
    last = k_frames;
    while (frame < 7000 && !k_exit()) {
        int repeat, plate_clear_from = -1;
        a = k_mus_plus();
        if (a < 0 && a > -16) break;
        /* 70 Hz steps since the start, counted exactly (a fast CPU draws
         * more often than that: wait for the next step) */
        while ((repeat = (k_frames - last) * 7 / 5 - done70) < 1 && !k_exit())
            k_waitvbl();
        done70 += repeat;
        while (repeat--) {
            frame++;
            rx += 32; ry += 7;
            rx %= 3 * 3600; ry %= 3 * 3600; rz %= 3 * 3600;
            if (frame > 900) {
                a = frame - 900;
                b = frame - 900; if (b > 50) b = 50;
                oxp = sin255(a * 3) * b / 10;
                oyp = sin255(a * 5) * b / 10;
                ozp = (sin255(a * 4) / 2 + 128) * b / 16;
                if (frame > 1800) {
                    a = frame - 1800 + 64;
                    if (a > 1024) a = 1024;
                    oxb = -sin255(a * 6) * a / 40;
                    oyb = -sin255(a * 7) * a / 40;
                    ozb = (sin255(a * 8) + 128) * a / 40;
                } else {
                    oxb = -sin255(a * 6);
                    oyb = -sin255(a * 7);
                    ozb = sin255(a * 8) + 128;
                }
                b = 1800 - frame;
                if (b < 0) {
                    if (b < -99) b = -99;
                    oyp -= b * b / 2;
                }
            }
            if (frame > 800) {
                if (frame > 1220 + 789) {
                    if (xscale > 0) xscale--;
                    if (yscale > 0) yscale--;
                    if (zscale > 0) zscale--;
                    if (bscale > 0) bscale--;
                } else if (frame > 1400 + 789) {
                    if (bscale > 0) bscale -= 8;
                    if (bscale < 0) bscale = 0;
                } else {
                    if (bscale < 180) bscale += 2;
                    else bscale = 180;
                }
                if (bscale > xscale) lightshift = 10;
            } else {
                if (frame < 640 + 70) {
                    yposa += 31;
                    ypos += yposa / 40;
                    if (ypos > -300) {
                        ypos -= yposa / 40;
                        yposa = -yposa * boingm / boingd;
                        boingm += 2;
                        boingd++;
                    }
                    if (ypos > -900 && yposa > 0) {
                        jello = (ypos + 900) * 5 / 3;
                        jelloa = 0;
                    }
                } else {
                    if (ypos > -2800) ypos -= 16;
                    else if (ypos < -2800) ypos += 16;
                }
                yscale = xscale = 120 + jello / 30;
                zscale = 120 - jello / 30;
                a = jello;
                jello += jelloa;
                if ((a < 0 && jello > 0) || (a > 0 && jello < 0))
                    jelloa = jelloa * 5 / 6;
                a = jello / 20;
                jelloa -= a;
            }
            /* background fades */
            if (frame > 1280 + 789) {
                b = 1280 + 789 + 64 - frame;
                base_fade = b < 0 ? 0 : b;
            } else if (frame > 700) {
                if (frame < 765) {
                    b = 764 - frame;
                    base_fade = b < 0 ? 0 : b;
                } else if (frame < 790) {
                    plate_clear_from = 150 + (frame - 765) * 2;
                    if (frame > 785) {
                        for (i = 0; i < 8; i++) {
                            int r = 0;
                            if (i & 1) r += 10;
                            if (i & 2) r += 30;
                            if (i & 4) r += 20;
                            base[i][0] = r > 63 ? 63 : r;
                            base[i][1] = 0;
                            base[i][2] = 0;
                        }
                    }
                } else if (frame < 795) {
                    base_fade = 64;
                }
            }
            if (plate_clear_from >= 0 && plate_clear_from < H) {
                /* erase two plate rows in both buffers */
                int k;
                for (k = 0; k < 2; k++) {
                    memset(buf[k] + plate_clear_from * BPR, 0, 2 * BPR);
                    memset(buf[k] + PL + plate_clear_from * BPR, 0, 2 * BPR);
                }
            }
        }
        /* ---- draw */
        cur ^= 1;
        {
            u8 *bf = buf[cur];
            int used_scratch = 0;
            if (frame > 800) clear_planes = 5;
            k_wait_blitter();
            if (lbh[cur] < 0 || lcp[cur] != clear_planes) {
                k_blit_clear(bf + (5 - clear_planes) * PL, 0, BPR / 2, H * clear_planes > 1023 ? 1000 : H * clear_planes);
                if (H * clear_planes > 1000)
                    k_blit_clear(bf + (5 - clear_planes) * PL + 1000 * BPR, 0, BPR / 2, H * clear_planes - 1000);
                lcp[cur] = clear_planes;
            } else if (lbh[cur] > 0) {
                int p;
                for (p = 5 - clear_planes; p < 5; p++)
                    k_blit_clear(bf + p * PL + lby[cur] * BPR + lbx[cur] * 2, BPR - 2 * lbw[cur], lbw[cur], lbh[cur]);
            }
            if (sbh > 0)
                k_blit_clear(scratch + sby * BPR + sbx * 2, BPR - 2 * sbw, sbw, sbh);
            sbh = 0;
            k_wait_blitter();
            bbx0 = bby0 = 32767;
            bbx1 = bby1 = -32768;
            if (xscale > 4) {
                matrix_yxz(rx, ry, rz, m);
                transform(pts_a, m, xscale, yscale, zscale, oxp, ypos + 1500 + oyp, zpos + ozp,
                          frame < 800);
                bbox_points();
                draw_a(bf);
            }
            if (frame > 800 && bscale > 4) {
                matrix_yxz(3600 - rx / 3, 3600 - ry / 3, 3600 - rz / 3, m);
                transform(pts_b, m, bscale, bscale, bscale, oxb, ypos + 1500 + oyb, zpos + ozb, 0);
                bbox_points();
                draw_b(bf, &used_scratch);
            }
            if (bbx0 < 0) bbx0 = 0;
            if (bbx1 > W - 1) bbx1 = W - 1;
            if (bby0 < 0) bby0 = 0;
            if (bby1 > H) bby1 = H;
            lbh[cur] = 0;
            if (used_scratch) {
                sbx = sby = 0;          /* (narrowed below) */
                sbw = BPR / 2;
                sbh = H;
            }
            if (bbx0 <= bbx1 && bby0 < bby1) {
                int wx0 = bbx0 >> 4, ww = (bbx1 >> 4) - wx0 + 1, p;
                lbx[cur] = wx0;
                lbw[cur] = ww;
                lby[cur] = bby0;
                lbh[cur] = (bby1 + 1 > H ? H : bby1 + 1) - bby0;
                if (used_scratch) {
                    sbx = wx0;
                    sbw = ww;
                    sby = bby0;
                    sbh = lbh[cur];
                }
                for (p = (frame > 800 ? 0 : 2); p < 5; p++)
                    xf_fill(bf + p * PL, BPR, wx0, ww, bby0, bby1 - bby0);
                if (used_scratch) {
                    xf_fill(scratch, BPR, wx0, ww, bby0, bby1 - bby0);
                    k_blit_or(scratch + bby0 * BPR + wx0 * 2, BPR - 2 * ww,
                              bf + 2 * PL + bby0 * BPR + wx0 * 2, BPR - 2 * ww, ww, bby1 - bby0, 0);
                }
            }
            k_wait_blitter();
            make_palette(cur);
        }
        k_set_copper(cop[cur]);
        k_waitvbl();
    }
    k_set_copper(k_empty_copper);
    k_waitvbl();
}
