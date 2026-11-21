/* Kernel internal definitions */
#ifndef KERNEL_INT_H
#define KERNEL_INT_H

#include "kernel.h"

/* fixed low memory map */
#define KERNEL_BASE 0x00400
#define SSP_TOP     0x0C000
#define MFM_BUF     0x0C000   /* 13312 bytes */
#define TRACK_BUF   0x0F400   /* 5632 bytes  */
#define CHUNK_BUF   0x10A00   /* 2 x 4096    */
#define CHUNK_SIZE  4096
#define FREE_START  0x12A00
#define CHIP_END    0x100000

#define DISK_ID(n) (0x53524400u | (n))   /* 'SRD' + disk number */

struct load_req {
    int disk;
    u32 offset;
    u32 len;
    u8 *dest;
    volatile int *done;
};

/* file directory entry, patched into the kernel image by the disk builder */
struct dir_entry {
    u8 disk;
    u8 flags;
    u16 margin;        /* in-place unpack safety margin */
    u32 offset;        /* byte offset on disk */
    u32 packed;        /* packed size */
    u32 size;          /* unpacked size */
};
#define DIR_MAX 128
extern struct dir_entry k_dir[DIR_MAX];

/* loader */
void k_loader_init(void);
void k_loader_step(void);
int k_loader_queue(int disk, u32 offset, u32 len, void *dest, volatile int *done);
int k_loader_busy(void);
extern volatile int k_loader_wanted_disk;
extern volatile u32 k_loader_errors;

/* music */
void k_music_init(void);
void k_music_start(const void *bank, const void *stream);
void k_music_stop(void);
void k_preload_after(int id);
void k_music_tick(void);
void k_music_dma(void);
void k_music_slow(void);
void k_music_fade(int speed);

/* memory */
void k_mem_init(void);
void k_mem_free_tag(int tag);

int k_get_sr(void);
void kmain(void);

#endif
