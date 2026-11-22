/*
 * The demo script: which music plays when and the order of the parts.
 * Mirrors the sequence in MAIN/U2.ASM of the original.
 */
#include "kernel_int.h"
#include "files.h"
#include "music_trim.h"

void k_run_part(int id);
void k_preload(int id);
void k_music_load_bank(int slot, int file_id);
void k_music_free_bank(int slot);
void k_music_play(int slot, int stream_id);

/* after a part: show its last colour from kernel memory so that its
 * memory is released before the next part is unpacked */
static void solid(u16 col)
{
    static u32 list[4];
    list[0] = CMOVE(R_BPLCON0, 0x0200);
    list[1] = CMOVE(R_COLOR00, col);
    list[2] = CEND;
    list[3] = CEND;
    k_set_copper(list);
}

void demo_main(void)
{
#ifdef TEST_PART
    {
        void k_music_skip_to(int order);
        k_music_load_bank(TEST_MUSIC, TEST_MUSIC ? F_MUS1_BANK : F_MUS0_BANK);
        k_music_play(TEST_MUSIC, TEST_MUSIC ? F_MUS1_S0 : TEST_ORDER >= 18 ? F_MUS0_S18 : F_MUS0_S0);
        k_set_ipl(6);
        k_music_skip_to(TEST_ORDER);
        k_set_ipl(0);
        for (;;)
            k_run_part(TEST_PART);
    }
#else
    /* ---- music 0: intro */
    k_music_load_bank(0, F_MUS0_BANK);
    k_music_play(0, F_MUS0_S0);
#ifndef SKIP_INTRO
    k_preload_after(F_PART_U2A);
    k_run_part(F_PART_ALKU);
    k_preload_after(F_PART_PAM);
    k_run_part(F_PART_U2A);
#else
    {
        void k_music_skip_to(int order);
        k_set_ipl(6);
        k_music_skip_to(12);
        k_set_ipl(0);
    }
#endif
    k_preload_after(F_PART_BEGLOGO);
    k_run_part(F_PART_PAM);
    {
        /* PAM ends on white: show the same from kernel memory so PAM's
         * buffers are released before the title is unpacked */
        static u32 white[4] = { CMOVE(R_BPLCON0, 0x0200), CMOVE(R_COLOR00, 0xfff), CEND, CEND };
        k_set_copper(white);
    }
    /* music 1 loads in the background while the title is shown */
    k_preload_after(F_MUS1_BANK);
    k_preload_after(F_MUS1_S0);
    k_preload_after(F_PART_GLENZ);
    k_run_part(F_PART_BEGLOGO);
    /* ---- music 1 */
    k_music_load_bank(1, F_MUS1_BANK);
    k_music_play(1, F_MUS1_S0);
#ifdef MUS1_TRIM_ORDER
    {
        void k_music_trim_at(int slot, int order, u32 bytes);
        k_music_trim_at(1, MUS1_TRIM_ORDER, MUS1_TRIM_BYTES);
    }
#endif
    k_music_free_bank(0);
    k_preload_after(F_PART_TUNNELI);
    k_run_part(F_PART_GLENZ);
    k_preload_after(F_PART_TECHNO);
    k_run_part(F_PART_TUNNELI);
    solid(0);
    k_run_part(F_PART_TECHNO);
    /* ---- disk 2 */
    {
        void k_need_disk(int n);
        k_need_disk(2);
    }
    k_preload_after(F_PART_MNTSCRL);
    k_run_part(F_PART_PANIC);
    /* original: wait until musplus goes below zero and back above */
    while (k_mus_plus() >= 0 && !k_mus_after(42, 1))
        k_waitvbl();
    while (k_mus_plus() <= 0 && !k_mus_after(42, 1))
        k_waitvbl();
    k_preload_after(F_PART_LENS);
    k_run_part(F_PART_MNTSCRL);
    k_set_copper(k_empty_copper);   /* frees MNTSCRL before LENS is unpacked */
    k_preload_after(F_PART_PLZ);
    k_run_part(F_PART_LENS);
    solid(0xfff);
    k_preload_after(F_PART_DOTS);
    k_run_part(F_PART_PLZ);
    solid(0);
    k_preload_after(F_PART_WATER);
    k_run_part(F_PART_DOTS);
    solid(0);
    k_preload_after(F_PART_COMAN);
    k_run_part(F_PART_WATER);
    solid(0);
    k_preload_after(F_PART_JPLOGO);
    k_preload_after(F_MUS0_BANK2);
    k_run_part(F_PART_COMAN);
    solid(0);
    k_preload_after(F_PART_U2E);
    k_run_part(F_PART_JPLOGO);
    /* ---- music 0 again: JPLOGO ended on white */
    {
        static u32 white[4] = { CMOVE(R_BPLCON0, 0x0200), CMOVE(R_COLOR00, 0xfff), CEND, CEND };
        void k_music_stop(void);
        k_set_copper(white);
        k_music_stop();
        k_music_free_bank(1);
        k_music_load_bank(0, F_MUS0_BANK2);
        k_music_play(0, F_MUS0_S18);
    }
    k_preload_after(F_PART_ENDLOGO);
    k_run_part(F_PART_U2E);
    solid(0xfff);
    k_preload_after(F_PART_CRED);
    k_run_part(F_PART_ENDLOGO);
    solid(0);
    k_preload_after(F_PART_ENDSCRL);
    k_run_part(F_PART_CRED);
    solid(0);
    k_run_part(F_PART_ENDSCRL);
#ifdef MEMSTAT
    {
        extern u32 k_memstat[32];
        extern void k_panic(u32 vec, u32 *regs, u16 *frame);
        static u16 fr[80];
        int q;
        for (q = 0; q < 16; q++) {
            fr[q * 2 + 7] = k_memstat[16 + q] >> 16;
            fr[q * 2 + 8] = k_memstat[16 + q];
        }
        k_panic(0xab, k_memstat, fr);
    }
#endif
    {
        extern void *k_whd_resload;
        void k_whd_quit(void);
        if (k_whd_resload)
            k_whd_quit();
    }
    for (;;)
        k_waitvbl();
#endif
}
