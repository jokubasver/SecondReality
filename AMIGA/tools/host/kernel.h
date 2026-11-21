/* host shim for running the VISU engine port natively */
#ifndef KERNEL_H
#define KERNEL_H
#include <string.h>
#include <stdio.h>
typedef unsigned char u8; typedef signed char s8; typedef unsigned short u16; typedef signed short s16;
typedef unsigned int u32; typedef signed int s32;
static inline s32 muls16(s16 a, s16 b) { return (s32)a * b; }
static inline u32 mulu16(u16 a, u16 b) { return (u32)a * b; }
static volatile u16 dummy16; static volatile u32 dummy32;
#define BLTCON0 dummy16
#define BLTCON1 dummy16
#define BLTAFWM dummy16
#define BLTALWM dummy16
#define BLTAPT dummy32
#define BLTBPT dummy32
#define BLTCPT dummy32
#define BLTDPT dummy32
#define BLTAMOD dummy16
#define BLTBMOD dummy16
#define BLTCMOD dummy16
#define BLTDMOD dummy16
#define BLTSIZE dummy16
#define DMACONR dummy16
static inline void k_wait_blitter(void) {}
extern void host_fill(int n, const s16 *xs, const s16 *ys, int color);
#define V3_HOOK_FILL(n, xs, ys, c) do { host_fill(n, xs, ys, c); return; } while (0)
#endif
