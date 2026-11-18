/*
 * ALKU - opening credits.
 *
 * Original (ALKU/MAIN.C, Wildfire): texts fade in and out on black, then a
 * 640x150 night horizon fades in and scrolls slowly while the credits fade in
 * over it, synchronised to the music via dis_sync().
 *
 * Amiga: 4 bitplanes of picture (16 colours, 656 pixels wide incl. a 16
 * pixel pad for hardware scrolling) plus a 5th plane for the credit text.
 * Colours 16..31 are the picture colours blended with the text colour, so
 * the text plane works as a transparent overlay that can be faded.
 */
#include "kernel.h"
#include "alku_data.h"

#define PIC_H 150
#define BPR 82                       /* bytes per row, 656 pixels */
#define PIC_LINE 0x61                /* PAL line of the picture top */
#define INTRO_LINE 0x48              /* PAL line of 200 line area top */

static u8 *textplane;
static u32 *cop;
static u16 colors[32];
static volatile int ncolors;
static volatile int vb_sx;           /* scroll position 0..320 */
static volatile int mode;            /* 0 intro, 1 picture */

static const u8 black[96 * 3];

static void vbl(void)
{
    int i;
    for (i = 0; i < ncolors; i++)
        COLOR(i) = colors[i];
    if (mode == 1) {
        /* X = first visible pixel in the buffer (16 pixel pad on the left);
         * the fetch starts one word early (DDFSTRT 0x30) */
        int X = vb_sx + 16;
        int pw = ((X + 15) >> 4) - 1, d = 16 * pw + 16 - X;
        u32 off = pw * 2;
        BPLPT(0) = (u32)pic_plane0 + off;
        BPLPT(1) = (u32)pic_plane1 + off;
        BPLPT(2) = (u32)pic_plane2 + off;
        BPLPT(3) = (u32)pic_plane3 + off;
        BPLPT(4) = (u32)textplane + off;
        BPLCON1 = d | (d << 4);
    }
}

/* time in 70 Hz VGA frames, as the original counted */
static u32 t0;
static void tstart(void) { t0 = k_frames; }
static int t70(void) { return (k_frames - t0) * 7 / 5; }

static void build_copper_pic(void)
{
    u32 *c = cop;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0030);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, BPR - 42);
    *c++ = CMOVE(R_BPL2MOD, BPR - 42);
    *c++ = CMOVE(R_BPLCON2, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CWAIT(PIC_LINE, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x5200);
    *c++ = CWAIT(PIC_LINE + PIC_H, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

static void build_copper_intro(const u8 *p0, const u8 *p1, int y, int h)
{
    u32 *c = cop;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPL2MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CMOVE(R_BPL1PTH, (u32)p0 >> 16);
    *c++ = CMOVE(R_BPL1PTH + 2, (u32)p0 & 0xffff);
    *c++ = CMOVE(R_BPL1PTH + 4, (u32)p1 >> 16);
    *c++ = CMOVE(R_BPL1PTH + 6, (u32)p1 & 0xffff);
    *c++ = CWAIT(INTRO_LINE + y, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x2200);
    *c++ = CWAIT(INTRO_LINE + y + h, 0x07);
    *c++ = CMOVE(R_BPLCON0, 0x0200);
    *c++ = CEND;
}

static void wait_sync(int n)
{
    while (k_mus_sync() < n && !k_exit())
        k_waitvbl();
}

/* fade the 4 intro colours: level 0..64 */
static void intro_fade(int level)
{
    k_fade_pal(black, text_cols, colors, 4, level);
    ncolors = 4;
}

static void intro_screen(int n)
{
    static const u8 *const p0[3] = { intro0_p0, intro1_p0, intro2_p0 };
    static const u8 *const p1[3] = { intro0_p1, intro1_p1, intro2_p1 };
    static const int ys[3] = { INTRO0_Y, INTRO1_Y, INTRO2_Y };
    static const int hs[3] = { INTRO0_H, INTRO1_H, INTRO2_H };
    int t;
    wait_sync(n + 1);
    intro_fade(0);
    k_waitvbl();
    build_copper_intro(p0[n], p1[n], ys[n], hs[n]);
    k_set_copper(cop);
    tstart();
    while ((t = t70()) < 64 + 300 + 64 && !k_exit()) {
        if (t < 64) intro_fade(t);
        else if (t < 364) intro_fade(64);
        else intro_fade(64 - (t - 364));
        k_waitvbl();
    }
    intro_fade(0);
    k_waitvbl();
}

/* picture palette: 'pic' = 0..64 fade from black, 'txt' = 0..64 text */
static u16 pictmp[16];
static void pic_colors(int pic, int txt)
{
    u16 blendtmp[16];
    u8 a[48], b[48];
    int i;
    for (i = 0; i < 48; i++) {
        a[i] = (pic_pal[i] * pic) >> 6;
        b[i] = (pic_blend[i] * pic) >> 6;
    }
    k_fade_pal(a, a, pictmp, 16, 0);
    k_fade_pal(a, b, blendtmp, 16, txt);
    for (i = 0; i < 16; i++) {
        colors[i] = pictmp[i];
        colors[16 + i] = blendtmp[i];
    }
    ncolors = 32;
}

static const u8 *const cred[4] = { cred0, cred1, cred2, cred3 };
static const int cred_y[4] = { CRED0_Y, CRED1_Y, CRED2_Y, CRED3_Y };
static const int cred_h[4] = { CRED0_H, CRED1_H, CRED2_H, CRED3_H };
static int cur_text = -1, drawn_sx = -1;

/* keep the credit text static on screen while the picture scrolls */
static void draw_text(int sx)
{
    int x = sx + 16, y, h;
    if (cur_text < 0 || sx == drawn_sx)
        return;
    y = cred_y[cur_text];
    h = cred_h[cur_text];
    k_blit_clear(textplane + y * BPR, 0, BPR / 2, h);
    k_blit_copy(cred[cur_text], -2, textplane + y * BPR + (x >> 4) * 2,
                BPR - 42, 21, h, x & 15);
    drawn_sx = sx;
}

static void set_text(int n)
{
    k_blit_clear(textplane, 0, BPR / 2, PIC_H);
    cur_text = n;
    drawn_sx = -1;
}

void part_main(void)
{
    int a, t, step, f, tptr;
    (void)pictmp;
    cop = k_alloc(128 * 4);
    textplane = k_alloc(BPR * PIC_H + BPR);
    ncolors = 0;
    mode = 0;
    intro_fade(0);
    k_vbl_hook = vbl;

    intro_screen(0);
    intro_screen(1);
    intro_screen(2);

    /* horizon */
    wait_sync(4);
    pic_colors(0, 0);
    vb_sx = 0;
    k_waitvbl();
    build_copper_pic();
    mode = 1;
    k_set_copper(cop);

    /* picture fades in (128 VGA frames) while scrolling */
    tstart();
    a = 0;
    for (;;) {
        t = t70();
        if (t >= 128 || k_exit()) break;
        a = t / 9;
        vb_sx = a;
        pic_colors(t * 64 / 128, 0);
        k_waitvbl();
    }
    /* credits: per scroll step state machine of the original */
    {
        int txt = 0, fade_dir = 0, fade_t = 0, fade_from = 0, waiting = 0;
        f = 60;
        tptr = 0;
        while (a < 320 && !k_exit()) {
            t = t70();
            step = t / 9;
            while (a < step && a < 320) {
                a++;
                if (waiting) {
                    if (!(a & 1) && k_mus_sync() >= 4 + tptr) {
                        waiting = 0;
                        f = 0;
                    }
                    continue;
                }
                if (f == 0) {
                    fade_dir = 1; fade_t = t; fade_from = txt; f += 20;
                } else if (f == 50) {
                    fade_dir = -1; fade_t = t; fade_from = txt; f++;
                } else if (f > 50 && fade_dir == 0) {
                    set_text(tptr < 4 ? tptr : -1);
                    tptr++;
                    waiting = 1;
                } else {
                    f++;
                }
            }
            if (fade_dir) {
                int l = t - fade_t;
                if (l > 64) l = 64;
                txt = fade_dir > 0 ? fade_from + l : fade_from - l;
                if (txt < 0) txt = 0;
                if (txt > 64) txt = 64;
                if (l >= 64) fade_dir = 0;
            }
            vb_sx = a;
            draw_text(a);
            pic_colors(64, txt);
            k_waitvbl();
        }
        /* fade remaining text out */
        if (txt) {
            int t1 = t70(), from = txt;
            while ((t = t70() - t1) < 64 && !k_exit()) {
                pic_colors(64, from - t * from / 64);
                k_waitvbl();
            }
        }
    }
    pic_colors(64, 0);
    k_waitvbl();
    /* Leave a self contained copper list showing the final picture: the
     * next part (U2A) continues on the same horizon. */
    {
        u32 *c = cop + 64;
        int X = vb_sx + 16, pw = ((X + 15) >> 4) - 1, d = 16 * pw + 16 - X, i;
        const u8 *pl[5];
        pl[0] = pic_plane0; pl[1] = pic_plane1; pl[2] = pic_plane2; pl[3] = pic_plane3;
        pl[4] = textplane;
        *c++ = CMOVE(R_DIWSTRT, 0x2c81);
        *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
        *c++ = CMOVE(R_DDFSTRT, 0x0030);
        *c++ = CMOVE(R_DDFSTOP, 0x00d0);
        *c++ = CMOVE(R_BPL1MOD, BPR - 42);
        *c++ = CMOVE(R_BPL2MOD, BPR - 42);
        *c++ = CMOVE(R_BPLCON1, d | (d << 4));
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        for (i = 0; i < 5; i++) {
            u32 a = (u32)pl[i] + pw * 2;
            *c++ = CMOVE(R_BPL1PTH + i * 4, a >> 16);
            *c++ = CMOVE(R_BPL1PTH + i * 4 + 2, a & 0xffff);
        }
        for (i = 0; i < 32; i++)
            *c++ = CMOVE(R_COLOR00 + i * 2, colors[i]);
        *c++ = CWAIT(PIC_LINE, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x5200);
        *c++ = CWAIT(PIC_LINE + PIC_H, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
        k_vbl_hook = NULL;
        k_set_copper(cop + 64);
    }
}
