/*
 * Second Reality Amiga - kernel API used by the demo parts.
 *
 * Parts are relocatable images linked against the kernel's symbols, so
 * every function and variable declared here can be used directly.
 */
#ifndef KERNEL_H
#define KERNEL_H

#include "hw.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ---- timing ------------------------------------------------------------ */
extern volatile u32 k_frames;            /* VBL counter (50 Hz) */
/* Waits for the next vertical blank; returns frames passed since the
 * previous call (at least 1).  Equivalent of dis_waitb(). */
int k_waitvbl(void);
/* Waits until the beam reaches line y (0..311). */
void k_waitline(int y);
/* User hooks, called from the level 3 interrupt. */
extern void (*volatile k_vbl_hook)(void);
extern void (*volatile k_copper_hook)(void);
/* Called from the blitter-finished interrupt; the part enables it with
 * INTENA = INTF_SETCLR | INTF_BLIT (switched off again when it returns). */
extern void (*volatile k_blit_hook)(void);

/* ---- part control ------------------------------------------------------ */
/* Nonzero if the part should end (left mouse button, debug builds only). */
int k_exit(void);
/* 64 byte inter-part message areas (like dis_msgarea). */
extern u8 k_msg[4][64];

/* ---- music ------------------------------------------------------------- */
extern volatile s16 k_mus_order;         /* current order index */
extern volatile s16 k_mus_row;           /* current row */
extern volatile s16 k_mus_plusflags;     /* bit0 marker coming, bit1 marker passed */
extern volatile s32 k_mus_frame;         /* music tick counter (settable) */
extern volatile u8 k_mus_volume;         /* master volume 0..64 */
/* dis_musplus(): -32..31 position relative to the nearest "+++" marker. */
int k_mus_plus(void);
int k_mus_after(int order, int row);   /* music at or past order/row */
int k_mus_playing(void);

/* ---- memory ------------------------------------------------------------ */
/* Allocate chip memory (everything is chip on the target).  Blocks with
 * tag MEM_PART are released automatically when the part returns. */
#define MEM_PART 0
#define MEM_KEEP 1
#define MEM_OLD 2                        /* previous part, freed when the next part shows */
void *k_alloc(u32 size);                 /* MEM_PART, 8 byte aligned, cleared */
void *k_alloc_tag(u32 size, int tag);
void k_free(void *p);
u32 k_mem_free_largest(void);

/* ---- files ------------------------------------------------------------- */
/* Loads and unpacks a data file; returns a pointer to MEM_PART memory. */
void *k_load(int file_id);
/* Size of an unpacked file. */
u32 k_file_size(int file_id);
unsigned int k_unpack(const void *packed, void *dest);

/* ---- display ----------------------------------------------------------- */
void k_set_copper(const void *list);     /* takes effect at next VBL */
void k_blank(void);                      /* black screen, bitplanes off */
/* keep the previous part's memory (shown picture) until released */
void k_hold_old(void);
void k_release_old(void);
void k_wait_blitter(void);
/* A copper list that shows nothing (colour 0 black). */
extern u32 k_empty_copper[];

/* ---- graphics helpers (gfx.c) ----------------------------------------- */
void k_blit_copy(const void *src, int smod, void *dst, int dmod, int w, int h, int shift);
void k_blit_or(const void *src, int smod, void *dst, int dmod, int w, int h, int shift);
void k_blit_clear(void *dst, int dmod, int w, int h);
void k_flush_cache(void);               /* after writing code */
u16 k_rgb(const u8 *c);                  /* 6 bit RGB triplet -> 0x0RGB */
void k_fade_pal(const u8 *a, const u8 *b, u16 *out, int n, int level); /* 0..64 */
int k_mus_sync(void);                    /* dis_sync() */
/* 16 bit free running E-clock counter (709379 Hz), for profiling */
u16 k_eclock(void);

/* ---- fast 16x16->32 multiplies --------------------------------------
 * (gcc's widening multiply recognition is disabled together with
 * -fexpensive-optimizations, so plain C would call __mulsi3) */
static inline s32 muls16(s16 a, s16 b)
{
    s32 r = a;
    __asm__("muls.w %1,%0" : "+d"(r) : "dmi"(b) : "cc");
    return r;
}
static inline u32 mulu16(u16 a, u16 b)
{
    u32 r = a;
    __asm__("mulu.w %1,%0" : "+d"(r) : "dmi"(b) : "cc");
    return r;
}

/* ---- misc -------------------------------------------------------------- */
void *memcpy(void *d, const void *s, unsigned int n);
void *memset(void *d, int c, unsigned int n);
void *memmove(void *d, const void *s, unsigned int n);
int memcmp(const void *a, const void *b, unsigned int n);
void k_set_ipl(int level);
u32 k_random(void);
void k_debug_color(u16 c);
/* debug builds: draws a 4 digit hex number into a bitplane */
void k_debug_hex(u8 *plane, int bpr, int xbyte, int y, u16 v);

/* Sine table: 1024 entries per full circle, amplitude 16384. */
extern const s16 k_sin1024[1024 + 256];
#define k_sin(a) (k_sin1024[(a) & 1023])
#define k_cos(a) (k_sin1024[((a) & 1023) + 256])

#endif
