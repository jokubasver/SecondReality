/* Test part: copper bars showing music order/row, flashing on each row. */
#include "kernel.h"

static u32 *cop;

void part_main(void)
{
    int i, lastrow = -1, flash = 0;
    u32 start = k_frames;
    cop = k_alloc(4 * 600);
    while (!k_exit() && k_frames - start < 50 * 30) {
        u32 *c = cop;
        int ord = k_mus_order, row = k_mus_row;
        if (row != lastrow) { flash = 15; lastrow = row; }
        *c++ = CMOVE(R_BPLCON0, 0x0200);
        for (i = 0; i < 64; i++) {
            u16 col;
            if (i < ord) col = 0x0a0;
            else col = 0x111;
            *c++ = CWAIT(0x40 + i, 0x07);
            *c++ = CMOVE(R_COLOR00, col);
        }
        for (i = 0; i < 64; i++) {
            *c++ = CWAIT(0x90 + i, 0x07);
            *c++ = CMOVE(R_COLOR00, i == row ? 0xff0 : (i < row ? 0x004 : 0x000));
        }
        *c++ = CWAIT(0xe0, 0x07);
        *c++ = CMOVE(R_COLOR00, flash << 8 | flash << 4 | flash);
        *c++ = CWAIT(0xe8, 0x07);
        *c++ = CMOVE(R_COLOR00, 0x000);
        *c++ = CEND;
        k_set_copper(cop);
        if (flash) flash--;
        k_waitvbl();
    }
}
