/*
 * CRED - the credits: 21 screens, a picture of the part slides in from
 * the right and the names rise from below, stay, and leave
 * (CREDITS/MAIN.C).  Amiga layout: picture above, names below; the screen
 * buffers are 640 pixels wide so the picture needs no clipping.
 */
#include "kernel.h"
#include "cred_data.h"

#define BPR 80                  /* 640 pixel buffer, the middle 320 shown */
#define PL (BPR * 200)
#define LINE0 0x48
#define PICY 6
#define TEXTY 114
#define TEXTH 86
#define LINEH 21

static u8 *scr[2], *tbuf, *ones;
static u32 *cop[2];
static u16 *colp[2];

/* as the released demo shows them (the sources differ on a few screens) */
static const char *const texts[NPICS] = {
    "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0",
    "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - PSI\0OBJECTS - WILDFIRE\0",
    "GRAPHICS - MARVEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0ANIMATION - TRUG\0",
    "\0GRAPHICS - PIXEL\0",
    "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0MUSIC - PURPLE MOTION\0CODE - TRUG\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0",
    "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - TRUG\0RENDERING - TRUG\0",
    "SKETCH - SKAVEN\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "SKETCH - SKAVEN\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "\0MUSIC - PURPLE MOTION\0CODE - WILDFIRE\0",
    "\0MUSIC - PURPLE MOTION\0CODE - WILDFIRE\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0CODE - TRUG\0RENDERING - TRUG\0",
    "\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "GRAPHICS - MARVEL\0MUSIC - PURPLE MOTION\0CODE - PSI\0",
    "MUSIC - SKAVEN\0CODE - PSI\0WORLD - TRUG\0",
    "\0GRAPHICS - PIXEL\0MUSIC - PURPLE MOTION\0",
    "GRAPHICS - PIXEL\0MUSIC - SKAVEN\0CODE - WILDFIRE\0",
};

/* text into tbuf (4 planes of 40 x TEXTH), centred, grey levels 1..9 */
static const char *print_line(int y, const char *t)
{
    int w = 0, x, k, r;
    const char *p;
    for (p = t; *p; p++)
        w += cfontab[(*p & 127) * 2 + 1] + 2;
    x = 160 - w / 2;
    for (p = t; *p; p++) {
        int sx = cfontab[(*p & 127) * 2], cw = cfontab[(*p & 127) * 2 + 1];
        for (k = 0; k < cw; k++, x++) {
            if (x < 0 || x >= 320) continue;
            for (r = 0; r < 16; r++) {
                int v = cfont[r * 1500 + sx + k], pl;
                if (!v) continue;
                for (pl = 0; pl < 4; pl++)
                    if (v & (1 << pl))
                        tbuf[pl * 40 * TEXTH + (y + r) * 40 + (x >> 3)] |= 0x80 >> (x & 7);
            }
        }
        x += 2;
    }
    return p + 1;
}

static void blit(const u8 *src, int smod, u8 *dst, int dmod, int w, int h, int shift, int fwm_ok)
{
    k_wait_blitter();
    BLTCON0 = 0x09f0 | (shift << 12);
    BLTCON1 = 0;
    BLTAFWM = 0xffff;
    BLTALWM = fwm_ok ? 0xffff : 0x0000;
    BLTAPT = (u32)src;
    BLTDPT = (u32)dst;
    BLTAMOD = smod;
    BLTDMOD = dmod;
    BLTSIZE = (h << 6) | w;
}

/* picture at buffer x (0..480), text at screen y offset dy */
static void compose(int b, int pic, int x, int dy)
{
    u8 *s = scr[b];
    int p, h;
    /* clear everything from the picture's first row on (the planes are
     * contiguous, rows 0..PICY-1 stay empty) */
    k_wait_blitter();
    BLTCON0 = 0x0100;
    BLTCON1 = 0;
    BLTDPT = (u32)(s + PICY * BPR);
    BLTDMOD = 0;
    BLTSIZE = ((5 * 200 - PICY) << 6) | 40;
    /* picture: 4 planes + plane 4 set (colours 16..31), 11 words with
     * the spill word masked */
    for (p = 0; p < 5; p++) {
        const u8 *src = p < 4 ? cpics + pic * 8000 + p * 2000 : ones;
        blit(src, -2, s + p * PL + PICY * BPR + (x >> 4) * 2, BPR - 22, 11, 100, x & 15, 0);
    }
    /* text */
    h = 200 - (TEXTY + dy);
    if (h > TEXTH) h = TEXTH;
    if (h > 0)
        for (p = 0; p < 4; p++)
            blit(tbuf + p * 40 * TEXTH, 0, s + p * PL + (TEXTY + dy) * BPR + 20, BPR - 40, 20, h, 0, 1);
    k_wait_blitter();
}

static void set_pal(int pic)
{
    int i, b;
    for (b = 0; b < 2; b++) {
        colp[b][1] = 0;
        for (i = 1; i < 16; i++) {
            int g = i < 10 ? 7 * i : 63;
            colp[b][i * 2 + 1] = (g >> 2) * 0x111;
        }
        for (i = 0; i < 16; i++) {
            const u8 *c = cpals + (pic * 16 + i) * 3;
            colp[b][(16 + i) * 2 + 1] = ((c[0] >> 2) << 8) | ((c[1] >> 2) << 4) | (c[2] >> 2);
        }
    }
}

void part_main(void)
{
    u32 *c;
    int i, b = 0, pic;
    for (i = 0; i < 2; i++) {
        scr[i] = k_alloc(PL * 5);
        cop[i] = k_alloc(64 * 4);
        c = cop[i];
        *c++ = CMOVE(R_DIWSTRT, 0x2c81);
        *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
        *c++ = CMOVE(R_DDFSTRT, 0x0038);
        *c++ = CMOVE(R_DDFSTOP, 0x00d0);
        *c++ = CMOVE(R_BPL1MOD, BPR - 40);
        *c++ = CMOVE(R_BPL2MOD, BPR - 40);
        *c++ = CMOVE(R_BPLCON1, 0);
        *c++ = CMOVE(R_BPLCON2, 0);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        colp[i] = (u16 *)c;
        for (pic = 0; pic < 32; pic++)
            *c++ = CMOVE(R_COLOR00 + pic * 2, 0);
        for (pic = 0; pic < 5; pic++) {
            u32 a = (u32)scr[i] + pic * PL + 20;
            *c++ = CMOVE(R_BPL1PTH + pic * 4, a >> 16);
            *c++ = CMOVE(R_BPL1PTH + pic * 4 + 2, a & 0xffff);
        }
        *c++ = CWAIT(LINE0, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x5200);
        *c++ = CWAIT_PAL;
        *c++ = CWAIT((LINE0 + 200) & 0xff, 0x07);
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        *c++ = CEND;
    }
    tbuf = k_alloc(40 * TEXTH * 4);
    ones = k_alloc(20 * 100);
    memset(ones, 0xff, 20 * 100);
    k_set_copper(cop[0]);
    k_waitvbl();
    for (pic = 0; pic < NPICS && !k_exit(); pic++) {
        const char *t = texts[pic];
        int y, v, line = 0;
        u32 t0, ph;
        memset(tbuf, 0, 40 * TEXTH * 4);
        /* like prtc(): a leading empty string leaves the first line empty */
        for (;;) {
            t = print_line(line * LINEH, t);
            line++;
            if (!*t || line >= 4) break;
        }
        set_pal(pic);
        /* slide in: y *= 12/13 per VGA frame */
        y = 200 * 128;
        t0 = k_frames;
        ph = 0;
        while (y > 0 && !k_exit()) {
            u32 now = (k_frames - t0) * 7 / 5;
            while (ph < now && y > 0) {
                y = (s32)y * 12 / 13;
                ph++;
            }
            compose(b, pic, 240 + y / 80, y / 256);
            k_set_copper(cop[b]);
            k_waitvbl();
            b ^= 1;
        }
        compose(b, pic, 240, 0);
        k_set_copper(cop[b]);
        b ^= 1;
        /* stay for 200 VGA frames */
        t0 = k_frames;
        while (!k_exit() && (k_frames - t0) * 7 / 5 < 200)
            k_waitvbl();
        /* leave: accelerating */
        y = 0;
        v = 0;
        t0 = k_frames;
        ph = 0;
        while (y < 200 * 128 && !k_exit()) {
            u32 now = (k_frames - t0) * 7 / 5;
            while (ph < now && y < 200 * 128) {
                y += v;
                v += 15;
                ph++;
            }
            if (y > 200 * 128) y = 200 * 128;
            compose(b, pic, 240 - y / 80, y / 256);
            k_set_copper(cop[b]);
            k_waitvbl();
            b ^= 1;
        }
    }
}
