/*
 * Second Reality Amiga - kernel core: interrupts, memory, files, parts.
 */
#include "kernel_int.h"
#include "../common/unlz.h"

volatile u32 k_frames;
volatile u32 k_dbg_phase, k_dbg_val;
#ifdef DEBUG
static volatile int dbg_overlay = 1;
#define PHASE(x) (k_dbg_phase = (x))
#else
#define PHASE(x)
#endif
void (*volatile k_vbl_hook)(void);
void (*volatile k_copper_hook)(void);
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

void k_level6(void)
{
    u8 icr = CIAB_ICR;
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
    for (;;) {
        u32 v = (*(volatile u32 *)0xdff004) >> 8;
        if ((int)(v & 0x1ff) == y)
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

void k_set_copper(const void *list)
{
    COP1LC = (u32)list;
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
        /* out of memory: flash and hang */
        for (;;) COLOR(0) = (u16)k_frames & 1 ? 0xf0f : 0x000;
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

#define MAXPRE 4
static struct { int id; u8 *buf; volatile int done; } pre[MAXPRE];

/* Starts loading a file's packed data in the background. */
void k_preload(int id)
{
    int i;
    for (i = 0; i < MAXPRE; i++) {
        if (pre[i].buf == NULL) {
            const struct dir_entry *d = &k_dir[id];
            pre[i].id = id;
            pre[i].buf = k_alloc_tag(d->packed, MEM_KEEP);
            k_loader_queue(d->disk, d->offset, d->packed, pre[i].buf, &pre[i].done);
            return;
        }
    }
}

int k_preload_done(int id)
{
    int i;
    for (i = 0; i < MAXPRE; i++)
        if (pre[i].buf && pre[i].id == id)
            return pre[i].done;
    return 0;
}

static void wait_done(volatile int *done)
{
    while (!*done) {
        /* show the disk request colour while waiting for a disk swap */
        if (k_loader_wanted_disk)
            COLOR(0) = (k_frames & 16) ? 0x400 : 0x000;

    }
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
        if (pre[i].buf && pre[i].id == id) {
            wait_done(&pre[i].done);
            dst = k_alloc_tag(d->size + extra + 8, tag);
            unlz(pre[i].buf, dst);
            k_free(pre[i].buf);
            pre[i].buf = NULL;
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
    img = (u8 *)&h->rel[h->nrel];
    for (i = 0; i < h->nrel; i++)
        *(u32 *)(img + h->rel[i]) += (u32)img;
    memset(img + h->image, 0, bss);
    entry = (void (*)(void))(img + h->entry);
    k_vbl_hook = NULL;
    k_copper_hook = NULL;
    PHASE(0x50000000 | id);
#ifdef DEBUG
    dbg_overlay = 0;
#endif
    entry();
#ifdef DEBUG
    dbg_overlay = 1;
#endif
    PHASE(0x5f000000 | id);
    k_vbl_hook = NULL;
    k_copper_hook = NULL;
    INTENA = INTF_COPER | INTF_BLIT;
    k_wait_blitter();
    k_mem_free_tag(MEM_PART);
}

/* ----------------------------------------------------------------- music */
static void *music_bank[2];
static void *music_stream;

void *k_music_bank(int slot) { return music_bank[slot]; }

void k_music_load_bank(int slot, int file_id)
{
    if (music_bank[slot])
        k_free(music_bank[slot]);
    music_bank[slot] = k_load_ex(file_id, 0, MEM_KEEP);
}

void k_music_free_bank(int slot)
{
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
    DMACON = DMAF_SETCLR | DMAF_MASTER | DMAF_COPPER | DMAF_BLITTER;
    k_loader_init();
    k_music_init();
    INTENA = INTF_SETCLR | INTF_INTEN | INTF_VERTB | INTF_EXTER;
    k_set_ipl(0);
    demo_main();
    for (;;)
        ;
}
