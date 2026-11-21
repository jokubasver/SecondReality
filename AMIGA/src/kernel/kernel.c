/*
 * Second Reality Amiga - kernel core: interrupts, memory, files, parts.
 */
#include "kernel_int.h"
unsigned int unlz_asm(const void *packed, void *dest);
#define unlz unlz_asm

volatile u32 k_frames;
volatile u32 k_dbg_phase, k_dbg_val;
#ifdef DEBUG
#ifdef DEBUG_OVERLAY
static volatile int dbg_overlay = 1;
#else
static volatile int dbg_overlay = 0;
#endif
#define PHASE(x) (k_dbg_phase = (x))
#else
#define PHASE(x)
#endif
void (*volatile k_vbl_hook)(void);
void (*volatile k_copper_hook)(void);
void (*volatile k_blit_hook)(void);
u8 k_msg[4][64];

/* patched by the disk builder (tools/mkdisk.py) */
struct dir_entry k_dir[DIR_MAX] __attribute__((section(".data.dir"))) = { { 0xee } };

/* ------------------------------------------------------------- interrupts */
static volatile int slow_busy;

void k_level2(void)
{
    (void)CIAA_ICR;
    INTREQ = INTF_PORTS;
    INTREQ = INTF_PORTS;
}

void k_level3(void)
{
    u16 r = INTREQR;
    if (r & INTF_COPER) {
        INTREQ = INTF_COPER;
        INTREQ = INTF_COPER;
        if (k_copper_hook)
            k_copper_hook();
    }
    if (r & INTF_BLIT) {
        INTREQ = INTF_BLIT;
        INTREQ = INTF_BLIT;
        if (k_blit_hook)
            k_blit_hook();
    }
    if (r & INTF_VERTB) {
        INTREQ = INTF_VERTB;
        INTREQ = INTF_VERTB;
        k_frames++;
        if (k_vbl_hook)
            k_vbl_hook();
#ifdef DEBUG
        if (dbg_overlay) {
            extern u32 k_loader_debug(void);
            extern void k_debug_status(u32, u32, u32, u32);
            k_debug_status(k_frames, k_loader_debug(), k_dbg_phase, k_dbg_val);
        }
#endif
        if (!slow_busy) {
            slow_busy = 1;
            k_set_ipl(2);              /* let music, copper and VBL preempt */
            k_music_slow();
            k_loader_step();
            k_set_ipl(3);
            slow_busy = 0;
        }
    }
}

u32 k_lev6_frame;
#ifdef DEBUG
/* watchdog: called from level 6 with the interrupted frame */
static u32 wd_last_frames, wd_ticks;
#endif
void k_level6(void)
{
    u8 icr = CIAB_ICR;
#ifdef DEBUG
    if (k_frames != wd_last_frames) {
        wd_last_frames = k_frames;
        wd_ticks = 0;
    } else if (++wd_ticks > 260) {
        extern void k_panic(u32 vec, u32 *regs, u16 *frame);
        static u32 r[16];
        r[0] = INTENAR; r[1] = INTREQR; r[2] = DMACONR; r[3] = k_dbg_phase;
        r[4] = slow_busy;
        k_panic(0xcc, r, (u16 *)k_lev6_frame);
    }
#endif
    if (icr & 1)
        k_music_tick();
    if (icr & 2)
        k_music_dma();
    INTREQ = INTF_EXTER;
    INTREQ = INTF_EXTER;
}

/* ---------------------------------------------------------------- timing */
static u32 last_wait;

int k_waitvbl(void)
{
    u32 f = k_frames;
    int d;
    while (k_frames == f)
        ;
    d = k_frames - last_wait;
    last_wait = k_frames;
    return d < 1 ? 1 : d;
}

void k_waitline(int y)
{
    /* at or past line y (an exact match can be missed every frame when an
     * interrupt runs over that line) */
    for (;;) {
        u32 v = (*(volatile u32 *)0xdff004) >> 8;
        if ((int)(v & 0x1ff) >= y)
            return;
    }
}

int k_exit(void)
{
#ifdef DEBUG
    return !(CIAA_PRA & 0x40);
#else
    return 0;
#endif
}

void k_debug_color(u16 c) { COLOR(0) = c; }

static u32 rnd_state = 0x12345678;
u32 k_random(void)
{
    rnd_state = rnd_state * 1103515245u + 12345u;
    return rnd_state >> 8;
}

/* --------------------------------------------------------------- display */
u32 k_empty_copper[] = {
    CMOVE(R_BPLCON0, 0x0200),
    CMOVE(R_COLOR00, 0x000),
    CEND, CEND
};

static void preload_kick(void);
static volatile int old_pending, old_hold;
static const void *cur_copper = k_empty_copper;

/* A part can keep using the previous part's memory (e.g. its picture)
 * until it calls k_release_old(). */
void k_hold_old(void) { old_hold = 1; }
void k_release_old(void)
{
    old_hold = 0;
    if (old_pending) {
        k_waitvbl();
        k_mem_free_tag(MEM_OLD);
        old_pending = 0;
        preload_kick();
    }
}

void k_set_copper(const void *list)
{
    cur_copper = list;
    COP1LC = (u32)list;
    DMACON = DMAF_SETCLR | DMAF_RASTER;    /* k_blank switches it off */
    if (old_pending && !old_hold) {
        /* the previous part's display stays up until now */
        k_waitvbl();
        k_mem_free_tag(MEM_OLD);
        old_pending = 0;
        preload_kick();
    }
}

void k_blank(void)
{
    k_set_copper(k_empty_copper);
    k_waitvbl();
    DMACON = DMAF_RASTER | DMAF_SPRITE;
    COLOR(0) = 0;
}

void k_wait_blitter(void)
{
    (void)DMACONR;
    while (DMACONR & DMAF_BLTDONE)
        ;
}

/* ---------------------------------------------------------------- memory */
#define MAXBLK 96
static struct { u32 start, end; int tag; } blk[MAXBLK];
static int nblk;

void k_mem_init(void) { nblk = 0; }

static u32 find_gap(u32 size)
{
    u32 pos = FREE_START;
    int i;
    for (i = 0; i <= nblk; i++) {
        u32 lim = i < nblk ? blk[i].start : CHIP_END;
        if (lim >= pos && lim - pos >= size)
            return pos;
        if (i < nblk && blk[i].end > pos)
            pos = (blk[i].end + 7) & ~7u;
    }
    return 0;
}

void *k_alloc_tag(u32 size, int tag)
{
    u32 p;
    int i, j;
    size = (size + 7) & ~7u;
    if (size == 0) size = 8;
    p = find_gap(size);
    if (!p || nblk >= MAXBLK) {
        /* out of memory: crash screen with size, largest free block,
         * block count, phase and the block list */
        extern void k_panic(u32 vec, u32 *regs, u16 *frame);
        static u32 info[16];
        static u16 fr[80];
        int k;
        info[0] = size;
        info[1] = k_mem_free_largest();
        info[2] = nblk;
        info[3] = k_dbg_phase;
        /* blocks: start | tag << 28 in the register area, sizes below */
        for (k = 0; k < 12 && k < nblk; k++)
            info[4 + k] = blk[k].start | ((u32)blk[k].tag << 28);
        for (k = 0; k < 16 && k < nblk; k++) {
            u32 sz = blk[k].end - blk[k].start;
            fr[k * 2 + 7] = sz >> 16;
            fr[k * 2 + 8] = sz;
        }
        k_panic(0xee, info, fr);
    }
    for (i = 0; i < nblk && blk[i].start < p; i++)
        ;
    for (j = nblk; j > i; j--)
        blk[j] = blk[j - 1];
    blk[i].start = p;
    blk[i].end = p + size;
    blk[i].tag = tag;
    nblk++;
    memset((void *)p, 0, size);
    return (void *)p;
}

void *k_alloc(u32 size) { return k_alloc_tag(size, MEM_PART); }

void k_free(void *ptr)
{
    int i;
    for (i = 0; i < nblk; i++) {
        if (blk[i].start == (u32)ptr) {
            for (; i < nblk - 1; i++)
                blk[i] = blk[i + 1];
            nblk--;
            return;
        }
    }
}

void k_mem_free_tag(int tag)
{
    int i, j = 0;
    for (i = 0; i < nblk; i++)
        if (blk[i].tag != tag)
            blk[j++] = blk[i];
    nblk = j;
}

u32 k_mem_free_largest(void)
{
    u32 pos = FREE_START, best = 0;
    int i;
    for (i = 0; i <= nblk; i++) {
        u32 lim = i < nblk ? blk[i].start : CHIP_END;
        if (lim > pos && lim - pos > best)
            best = lim - pos;
        if (i < nblk && blk[i].end > pos)
            pos = (blk[i].end + 7) & ~7u;
    }
    return best;
}

/* ----------------------------------------------------------------- files */
unsigned int k_unpack(const void *packed, void *dest) { return unlz(packed, dest); }

u32 k_file_size(int id) { return k_dir[id].size; }

#define MAXPRE 6
/* state: 0 free, 1 pending (no memory yet), 2 loading/loaded,
 * 3 waiting for the next part to be unpacked */
static struct { int id; int state; u8 *buf; volatile int done; } pre[MAXPRE];

/* Starts pending preloads for which there is memory now (in order). */
static void preload_kick(void)
{
    int i;
    for (i = 0; i < MAXPRE; i++) {
        if (pre[i].state == 1) {
            const struct dir_entry *d = &k_dir[pre[i].id];
            if (k_mem_free_largest() < d->packed + 64)
                return;          /* keep the order: wait for more memory */
            pre[i].buf = k_alloc_tag(d->packed, MEM_KEEP);
            pre[i].state = 2;
            k_loader_queue(d->disk, d->offset, d->packed, pre[i].buf, &pre[i].done);
        }
    }
}

/* Starts loading a file's packed data in the background (as soon as
 * there is memory for it). */
void k_preload(int id)
{
    int i;
    for (i = 0; i < MAXPRE; i++) {
        if (pre[i].state == 0) {
            pre[i].id = id;
            pre[i].state = 1;
            pre[i].done = 0;
            preload_kick();
            return;
        }
    }
}

/* Like k_preload, but the loading starts only after the next part has
 * been unpacked (so big preloads do not fragment the memory it needs). */
void k_preload_after(int id)
{
    int i;
    for (i = 0; i < MAXPRE; i++) {
        if (pre[i].state == 0) {
            pre[i].id = id;
            pre[i].state = 3;
            pre[i].done = 0;
            return;
        }
    }
}

static void release_after(void)
{
    int i;
    for (i = 0; i < MAXPRE; i++)
        if (pre[i].state == 3)
            pre[i].state = 1;
    preload_kick();
}

int k_preload_done(int id)
{
    int i;
    for (i = 0; i < MAXPRE; i++)
        if (pre[i].state == 2 && pre[i].id == id)
            return pre[i].done;
    return 0;
}

static void wait_done(volatile int *done)
{
    int waited = !*done;
#ifdef DEBUG
    if (waited) dbg_overlay = 1;
#endif
    while (!*done) {
        /* show the disk request colour while waiting for a disk swap */
        if (k_loader_wanted_disk)
            ;
    }
#if defined(DEBUG) && !defined(DEBUG_OVERLAY)
    if (dbg_overlay)
        dbg_overlay = 0;
#endif
#ifndef DEBUG_OVERLAY
    if (waited) {
        k_waitvbl();
        COP1LC = (u32)cur_copper;
    }
#endif
}

/* Loads + unpacks file 'id' into memory with 'extra' zeroed bytes after it. */
void *k_load_ex(int id, u32 extra, int tag)
{
    const struct dir_entry *d = &k_dir[id];
    u32 total = d->size + d->margin + 8;
    u8 *dst;
    int i;
    if (total < d->packed + 8) total = d->packed + 8;
    if (total < d->size + extra) total = d->size + extra;
    for (i = 0; i < MAXPRE; i++) {
        if (pre[i].state && pre[i].id == id) {
            if (pre[i].state == 1 || pre[i].state == 3) {
                /* not started (no memory yet, or deferred): load it directly */
                pre[i].state = 0;
                break;
            }
            PHASE(0x11000000 | id);
            wait_done(&pre[i].done);
            PHASE(0x12000000 | id);
            dst = k_alloc_tag(d->size + extra + 8, tag);
            k_dbg_val = (u32)dst;
            PHASE(0x13000000 | id);
            if (d->flags & 1)
                memcpy(dst, pre[i].buf, d->packed);     /* stored */
            else
                unlz(pre[i].buf, dst);
            PHASE(0x14000000 | id);
            k_free(pre[i].buf);
            pre[i].buf = NULL;
            pre[i].state = 0;
            preload_kick();
            return dst;
        }
    }
    dst = k_alloc_tag(total, tag);
    {
        u8 *src = dst + ((d->size + d->margin + 8 - d->packed) & ~1u);
        volatile int done;
        if (d->size + d->margin + 8 < d->packed) src = dst;
        PHASE(0x10000000 | id);
        k_loader_queue(d->disk, d->offset, d->packed, src, &done);
        wait_done(&done);
        PHASE(0x20000000 | id);
        if (d->flags & 1)
            memmove(dst, src, d->packed);     /* stored uncompressed */
        else
            unlz(src, dst);
        if (extra)
            memset(dst + d->size, 0, total - d->size);
        PHASE(0x30000000 | id);
    }
    return dst;
}

void *k_load(int id) { return k_load_ex(id, 0, MEM_PART); }

/* ----------------------------------------------------------------- parts */
struct part_hdr {
    u32 magic;
    u32 image;
    u32 bss;
    u32 entry;
    u32 nrel;
    u32 rel[1];
};

void k_run_part(int id)
{
    u8 *f;
    struct part_hdr *h;
    u8 *img;
    u32 i, bss;
    void (*entry)(void);
    {
        /* read header size first (bss unknown until unpacked): allocate
         * generously using a second pass */
        PHASE(0x40000000 | id);
        f = k_load_ex(id, 0, MEM_PART);
        h = (struct part_hdr *)f;
        bss = h->bss;
        if (bss) {
            /* reload with room for bss: copy into a fresh block */
            u8 *g = k_alloc_tag(k_dir[id].size + bss + 8, MEM_PART);
            memcpy(g, f, k_dir[id].size);
            k_free(f);
            f = g;
            h = (struct part_hdr *)f;
        }
    }
    release_after();
    img = (u8 *)&h->rel[h->nrel];
    for (i = 0; i < h->nrel; i++)
        *(u32 *)(img + h->rel[i]) += (u32)img;
    memset(img + h->image, 0, bss);
    entry = (void (*)(void))(img + h->entry);
    k_vbl_hook = NULL;
    k_copper_hook = NULL;
    PHASE(0x50000000 | id);
    entry();
    PHASE(0x5f000000 | id);
    k_vbl_hook = NULL;
    k_copper_hook = NULL;
    INTENA = INTF_COPER | INTF_BLIT;
    k_blit_hook = NULL;
    k_wait_blitter();
    /* keep the part's memory (its last copper list may still be shown)
     * until the next part sets up its own display */
    k_mem_free_tag(MEM_OLD);
    {
        int i;
        for (i = 0; i < nblk; i++)
            if (blk[i].tag == MEM_PART)
                blk[i].tag = MEM_OLD;
    }
    old_pending = 1;
}

/* ----------------------------------------------------------------- music */
static void *music_bank[2];
static void *music_stream;

void *k_music_bank(int slot) { return music_bank[slot]; }

void k_music_load_bank(int slot, int file_id)
{
    PHASE(0x70000000 | file_id);
    if (music_bank[slot])
        k_free(music_bank[slot]);
    music_bank[slot] = k_load_ex(file_id, 0, MEM_KEEP);
}

void k_music_free_bank(int slot)
{
    PHASE(0x71000000 | slot);
    if (music_bank[slot])
        k_free(music_bank[slot]);
    music_bank[slot] = NULL;
}

void k_music_play(int slot, int stream_id)
{
    k_music_stop();
    if (music_stream)
        k_free(music_stream);
    music_stream = k_load_ex(stream_id, 0, MEM_KEEP);
    PHASE(0x60000000 | stream_id);
    k_music_start(music_bank[slot], music_stream);
    PHASE(0x61000000 | stream_id);
}

/* ------------------------------------------------------------------ main */
void demo_main(void);

void kmain(void)
{
    DMACON = 0x7fff;
    INTENA = 0x7fff;
    INTREQ = 0x7fff;
    COLOR(0) = 0;
    BPLCON0 = 0x0200;
    k_mem_init();
    k_set_copper(k_empty_copper);
    COPJMP1 = 0;
    DMACON = DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER | DMAF_BLITTER | DMAF_RASTER;
    k_loader_init();
    k_music_init();
    {
        extern void k_eclock_init(void);
        k_eclock_init();
    }
    INTENA = INTF_SETCLR | INTF_INTEN | INTF_VERTB | INTF_EXTER;
    k_set_ipl(0);
    demo_main();
    for (;;)
        ;
}

/* ----------------------------------------------------- disk swapping */
#include "kmsg.h"
extern int k_loader_current_disk(void);
static u32 diskcop[24];

/* Makes sure disk n is in the drive, asking for it if necessary.  Music
 * keeps playing. */
void k_need_disk(int n)
{
    volatile int done = 0;
    static u8 tmp[4];
    const u8 *msg = n == 2 ? kmsg_disk2 : kmsg_disk1;
    const void *prev = cur_copper;
    int shown = 0;
    if (k_loader_current_disk() == n)
        return;
    /* reading one byte from disk n makes the loader identify the disk */
    k_loader_queue(n, 1024 + 512 * 11 - 4, 2, tmp, &done);
    while (!done) {
        if (k_loader_wanted_disk && !shown) {
            u32 *c = diskcop;
            *c++ = CMOVE(R_DIWSTRT, 0x2c81);
            *c++ = CMOVE(R_DIWSTOP, 0x2cc1);
            *c++ = CMOVE(R_DDFSTRT, 0x0038);
            *c++ = CMOVE(R_DDFSTOP, 0x00d0);
            *c++ = CMOVE(R_BPL1MOD, 0);
            *c++ = CMOVE(R_BPLCON1, 0);
            *c++ = CMOVE(R_BPLCON0, 0x0200);
            *c++ = CMOVE(R_COLOR00, 0x000);
            *c++ = CMOVE(R_COLOR00 + 2, 0xccd);
            *c++ = CMOVE(R_BPL1PTH, (u32)msg >> 16);
            *c++ = CMOVE(R_BPL1PTH + 2, (u32)msg & 0xffff);
            *c++ = CWAIT(0x2c + 120, 0x07);
            *c++ = CMOVE(R_BPLCON0, 0x1200);
            *c++ = CWAIT(0x2c + 120 + KMSG_H, 0x07);
            *c++ = CMOVE(R_BPLCON0, 0x0200);
            *c++ = CEND;
            COP1LC = (u32)diskcop;
            shown = 1;
        }
    }
    if (shown) {
        k_waitvbl();
        COP1LC = (u32)prev;
    }
}
