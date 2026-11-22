/* host shim with a software blitter (the modes the VISU engine uses), so
 * the engine's fill code runs unchanged: link with -no-pie so that static
 * buffers have 32 bit addresses */
#ifndef KERNEL_H
#define KERNEL_H
#include <string.h>
#include <stdio.h>
#include <stdint.h>
typedef unsigned char u8; typedef signed char s8; typedef unsigned short u16; typedef signed short s16;
typedef unsigned int u32; typedef signed int s32;
static inline s32 muls16(s16 a, s16 b) { return (s32)a * b; }
static inline u32 mulu16(u16 a, u16 b) { return (u32)a * b; }
extern u16 BLTCON0, BLTCON1, BLTAFWM, BLTALWM, BLTAMOD, BLTBMOD, BLTCMOD, BLTDMOD, BLTSIZE, DMACONR;
extern u32 BLTAPT, BLTBPT, BLTCPT, BLTDPT;
void k_wait_blitter(void);
#endif
