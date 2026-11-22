/* Test part / CPU meter: counts busy loop iterations per second (the CPU
 * time left over) and the raster lines the music mixer used, as hex:
 *   line 1: free loop iterations per second (high, low word)
 *   line 2: mixer raster lines per second, music order and row */
#include "kernel.h"

extern volatile u32 k_mix_lines;

void part_main(void)
{
    u8 *pl = k_alloc(40 * 200);
    u32 *cop = k_alloc(4 * 32), *c = cop;
    u32 start = k_frames, cnt = 0, f, sec = k_frames, ml = k_mix_lines;
    *c++ = CMOVE(R_DIWSTRT, 0x2c81);
    *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
    *c++ = CMOVE(R_DDFSTRT, 0x0038);
    *c++ = CMOVE(R_DDFSTOP, 0x00d0);
    *c++ = CMOVE(R_BPL1MOD, 0);
    *c++ = CMOVE(R_BPLCON1, 0);
    *c++ = CMOVE(R_BPL1PTH, (u32)pl >> 16);
    *c++ = CMOVE(R_BPL1PTH + 2, (u32)pl & 0xffff);
    *c++ = CMOVE(R_COLOR00, 0x003);
    *c++ = CMOVE(R_COLOR00 + 2, 0xfff);
    *c++ = CMOVE(R_BPLCON0, 0x1200);
    *c++ = CEND;
    k_set_copper(cop);
    while (!k_exit() && k_frames - start < 50 * 60) {
        f = k_frames;
        while (k_frames == f)
            cnt++;
        if (k_frames - sec >= 50) {
            u32 m = k_mix_lines;
#ifdef DEBUG
            k_debug_hex(pl, 40, 2, 40, cnt >> 16);
            k_debug_hex(pl, 40, 12, 40, cnt);
            k_debug_hex(pl, 40, 2, 80, m - ml);
            k_debug_hex(pl, 40, 12, 80, (k_mus_order << 8) | k_mus_row);
#endif
            ml = m;
            cnt = 0;
            sec += 50;
        }
    }
}
