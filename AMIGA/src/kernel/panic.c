/* Crash screen: shows exception vector, PC, SR, fault address, registers. */
#include "kernel_int.h"

/* 3x5 hex digit font, one u16 per glyph (5 rows x 3 bits) */
static const u16 hexfont[16] = {
    075557, 026227, 071747, 071717, 055711, 074717, 074757, 071111,
    075757, 075717, 075755, 065656, 074447, 065556, 074647, 074644,
};
#define PLANE ((u8 *)0x70000)
static u8 *plane = PLANE;

static void putdigit(int x, int y, int d)
{
    u16 g = hexfont[d & 15];
    int r, c;
    for (r = 0; r < 5; r++) {
        int bits = (g >> ((4 - r) * 3)) & 7;
        for (c = 0; c < 3; c++) {
            if (bits & (4 >> c)) {
                int px = x * 4 + c, py = y * 7 + r;
                int sx, sy;
                for (sy = 0; sy < 2; sy++)
                    for (sx = 0; sx < 2; sx++) {
                        int X = px * 2 + sx, Y = py * 2 + sy;
                        plane[Y * 40 + (X >> 3)] |= 0x80 >> (X & 7);
                    }
            }
        }
    }
}

static void puthex(int x, int y, u32 v, int n)
{
    int i;
    for (i = 0; i < n; i++)
        putdigit(x + i, y, (v >> ((n - 1 - i) * 4)) & 15);
}

static u32 cop[16];

void k_panic(u32 vec, u32 *regs, u16 *frame)
{
    int i;
    u32 pc, sr, addr = 0;
    u32 *c = cop;
    DMACON = 0x7fff;
    INTENA = 0x7fff;
    for (i = 0; i < 4; i++) AUDVOL(i) = 0;
    memset(PLANE, 0, 40 * 256);
    if (vec == 2 || vec == 3) {      /* 68000 group 0 frame */
        addr = ((u32)frame[1] << 16) | frame[2];
        sr = frame[4];
        pc = ((u32)frame[5] << 16) | frame[6];
    } else {
        sr = frame[0];
        pc = ((u32)frame[1] << 16) | frame[2];
    }
    puthex(1, 1, vec, 2);
    puthex(5, 1, pc, 8);
    puthex(15, 1, sr, 4);
    puthex(21, 1, addr, 8);
    for (i = 0; i < 16; i++)
        puthex(1 + (i & 3) * 9, 3 + (i >> 2), regs[i], 8);
    /* a few words of stack */
    for (i = 0; i < 16; i++)
        puthex(1 + (i & 3) * 9, 8 + (i >> 2), ((u32)frame[i * 2 + 7] << 16) | frame[i * 2 + 8], 8);
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPLCON0, 0x1200);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPL1PTH, 0x0007);
    *c++ = CMOVE(R_BPL1PTH + 2, 0x0000);
    *c++ = CMOVE(R_COLOR00, 0x500);
    *c++ = CMOVE(R_COLOR00 + 2, 0xfff);
    *c++ = CEND;
    COP1LC = (u32)cop;
    COPJMP1 = 0;
    DMACON = DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER | DMAF_RASTER;
    for (;;)
        ;
}

#ifdef DEBUG
/* Debug status line (kernel screens only): 1 plane, 10 rows at 0x7F000.
 * Glyphs are pre-expanded to one byte per row so drawing is cheap. */
static u8 dbgplane[40 * 10] __attribute__((aligned(4)));
#define DBG dbgplane
static u32 dbgcop[16];
static u8 glyph[16][5];
static int dbg_init;
static void dbg_hex(int cell, u32 v)
{
    int i, r;
    for (i = 0; i < 8; i++) {
        const u8 *g = glyph[(v >> ((7 - i) * 4)) & 15];
        u8 *d = DBG + cell + i;
        for (r = 0; r < 5; r++) {
            d[0] = g[r];
            d[40] = g[r];
            d += 80;
        }
    }
}
void k_debug_status(u32 a, u32 b, u32 c, u32 d)
{
    u32 *cp = dbgcop;
    if (!dbg_init) {
        int n, r, k;
        for (n = 0; n < 16; n++)
            for (r = 0; r < 5; r++) {
                int bits = (hexfont[n] >> ((4 - r) * 3)) & 7;
                u8 v = 0;
                for (k = 0; k < 3; k++)
                    if (bits & (4 >> k)) v |= 0xc0 >> (k * 2);
                glyph[n][r] = v;
            }
        memset(DBG, 0, 40 * 10);
        dbg_init = 1;
    }
    dbg_hex(0, a);
    dbg_hex(9, b);
    dbg_hex(18, c);
    dbg_hex(27, d);
    *cp++ = CMOVE(R_DIWSTRT, 0x2c81);
    *cp++ = CMOVE(R_DIWSTOP, 0x36c1);
    *cp++ = CMOVE(R_DDFSTRT, 0x0038);
    *cp++ = CMOVE(R_DDFSTOP, 0x00d0);
    *cp++ = CMOVE(R_BPLCON0, 0x1200);
    *cp++ = CMOVE(R_BPL1MOD, 0);
    *cp++ = CMOVE(R_BPL1PTH, (u32)DBG >> 16);
    *cp++ = CMOVE(R_BPL1PTH + 2, (u32)DBG & 0xffff);
    *cp++ = CMOVE(R_COLOR00, 0x000);
    *cp++ = CMOVE(R_COLOR00 + 2, 0xfff);
    *cp++ = CWAIT(0x36, 0x07);
    *cp++ = CMOVE(R_BPLCON0, 0x0200);
    *cp++ = CEND;
    COP1LC = (u32)dbgcop;
    DMACON = DMAF_SETCLR | DMAF_RASTER;
}
#endif

#ifdef DEBUG
static const u8 font57[16][7] = {
    { 0x38, 0x44, 0x4c, 0x54, 0x64, 0x44, 0x38 },
    { 0x10, 0x30, 0x10, 0x10, 0x10, 0x10, 0x38 },
    { 0x38, 0x44, 0x04, 0x08, 0x10, 0x20, 0x7c },
    { 0x78, 0x04, 0x04, 0x38, 0x04, 0x04, 0x78 },
    { 0x08, 0x18, 0x28, 0x48, 0x7c, 0x08, 0x08 },
    { 0x7c, 0x40, 0x78, 0x04, 0x04, 0x44, 0x38 },
    { 0x18, 0x20, 0x40, 0x78, 0x44, 0x44, 0x38 },
    { 0x7c, 0x04, 0x08, 0x10, 0x20, 0x20, 0x20 },
    { 0x38, 0x44, 0x44, 0x38, 0x44, 0x44, 0x38 },
    { 0x38, 0x44, 0x44, 0x3c, 0x04, 0x08, 0x30 },
    { 0x38, 0x44, 0x44, 0x7c, 0x44, 0x44, 0x44 },
    { 0x78, 0x44, 0x44, 0x78, 0x44, 0x44, 0x78 },
    { 0x38, 0x44, 0x40, 0x40, 0x40, 0x44, 0x38 },
    { 0x70, 0x48, 0x44, 0x44, 0x44, 0x48, 0x70 },
    { 0x7c, 0x40, 0x40, 0x78, 0x40, 0x40, 0x7c },
    { 0x7c, 0x40, 0x40, 0x78, 0x40, 0x40, 0x40 },
};
/* Draws a 4 digit hex number into a bitplane (debug builds), with a
 * cleared border so it is readable on any background. */
void k_debug_hex(u8 *plane, int bpr, int xbyte, int y, u16 v)
{
    int i, r;
    for (i = 0; i < 4; i++) {
        const u8 *g = font57[(v >> ((3 - i) * 4)) & 15];
        u8 *d = plane + y * bpr + xbyte + i * 2;
        for (r = 0; r < 18; r++, d += bpr) {
            u8 b = (r < 2 || r >= 16) ? 0 : g[(r - 2) >> 1];
            u16 w = 0;
            int k;
            for (k = 0; k < 8; k++)
                if (b & (0x80 >> k)) w |= 0xc000 >> (k * 2);
            d[0] = w >> 8;
            d[1] = w;
        }
    }
}
#endif
