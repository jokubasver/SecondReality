/*
 * The demo script: which music plays when and the order of the parts.
 * Mirrors the sequence in MAIN/U2.ASM of the original.
 */
#include "kernel_int.h"
#include "files.h"

void k_run_part(int id);
void k_preload(int id);
void k_music_load_bank(int slot, int file_id);
void k_music_free_bank(int slot);
void k_music_play(int slot, int stream_id);

void demo_main(void)
{
#ifdef TEST_PART
    {
        void k_music_skip_to(int order);
        k_music_load_bank(TEST_MUSIC, TEST_MUSIC ? F_MUS1_BANK : F_MUS0_BANK);
        k_music_play(TEST_MUSIC, TEST_MUSIC ? F_MUS1_S0 : F_MUS0_S0);
        k_set_ipl(6);
        k_music_skip_to(TEST_ORDER);
        k_set_ipl(0);
        for (;;)
            k_run_part(TEST_PART);
    }
#else
    k_music_load_bank(0, F_MUS0_BANK);
    k_music_play(0, F_MUS0_S0);
    k_preload(F_PART_U2A);
    k_run_part(F_PART_ALKU);
    k_preload(F_PART_PAM);
    k_run_part(F_PART_U2A);
    k_run_part(F_PART_PAM);
    k_music_load_bank(1, F_MUS1_BANK);
    k_music_play(1, F_MUS1_S0);
    for (;;)
        k_run_part(F_PART_TEST);
#endif
}
