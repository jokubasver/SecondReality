/*
 * Background MFM trackloader for DF0:.
 *
 * Runs as a state machine stepped once per vertical blank (from the
 * low-priority part of the level 3 interrupt), so parts keep running while
 * data streams in.  Disk layout is plain: byte offset = track*5632+sector*512
 * with track = cylinder*2 + head.
 */
#include "kernel_int.h"

#define MFM_WORDS 6656            /* 13312 bytes: > one revolution + 1 sector */
#define TRACK_BYTES 5632

static u16 *const mfmbuf = (u16 *)MFM_BUF;
static u8 *const trackbuf = (u8 *)TRACK_BUF;

enum { S_IDLE, S_SPINUP, S_SEEK0, S_STEP, S_SETTLE, S_READ, S_CHECKID, S_DONE_TRACK };

static int drv;                       /* 0 = DF0, 1 = DF1 */
static int drv_cyl[2] = { -1, -1 };   /* head position per drive */
static struct {
    int state;
    int cyl;          /* current head position, -1 unknown */
    int dir;          /* last step direction */
    int disk;         /* disk number known to be in the drive, -1 unknown */
    int cached_track; /* track decoded in trackbuf, -1 none */
    int want_track;
    int wait;
    int retries;
    int motor;
    int idle_frames;
} L = { S_IDLE, -1, 0, -1, -1, 0, 0, 0, 0, 0 };

#define QSIZE 16
static struct load_req queue[QSIZE];
static volatile int q_head, q_tail;

/* WHDLoad: both disk images in memory (set before bss is cleared, so
 * they live in .data) */
u8 *k_ramdisk __attribute__((section(".data"))) = 0;
#define RAM_CHUNK 12288                /* bytes copied per frame */

volatile int k_loader_wanted_disk;   /* nonzero while waiting for a disk swap */
volatile u32 k_loader_errors;

/* ---- drive control (CIA-B PRB, active low) ---- */
#define PRB_STEP 0x01
#define PRB_DIR  0x02
#define PRB_SIDE 0x04
#define PRB_SEL0 0x08
#define PRB_MTR  0x80

static void drive_motor(int on)
{
    u8 v = CIAB_PRB | 0x78;            /* deselect all */
    CIAB_PRB = v;
    if (on) v &= ~PRB_MTR; else v |= PRB_MTR;
    CIAB_PRB = v;
    CIAB_PRB = v & ~(PRB_SEL0 << drv); /* select the drive, latching motor state */
    L.motor = on;
}

/* Switches to the other drive (used while looking for a disk). */
static void other_drive(void)
{
    drive_motor(0);
    drv_cyl[drv] = L.cyl;
    drv ^= 1;
    L.cyl = drv_cyl[drv];
    L.disk = -1;
    L.cached_track = -1;
}

static void drive_side(int head)
{
    if (head) CIAB_PRB &= ~PRB_SIDE;   /* low = upper head (side 1) */
    else CIAB_PRB |= PRB_SIDE;
}

static void drive_step(int inward)
{
    u8 v = CIAB_PRB;
    if (inward) v &= ~PRB_DIR; else v |= PRB_DIR;
    CIAB_PRB = v;
    CIAB_PRB = v & ~PRB_STEP;
    CIAB_PRB = v & ~PRB_STEP;          /* short pulse */
    CIAB_PRB = v | PRB_STEP;
}

static int drive_track0(void) { return !(CIAA_PRA & 0x10); }

/* ---- MFM decoding ---- */
#define M 0x55555555u
static inline u32 rd32(const u16 *p) { return ((u32)p[0] << 16) | p[1]; }

/* Decodes all sectors found in the raw buffer.  Returns a bitmask of
 * successfully decoded sectors belonging to 'track'. */
static int decode_track(int track)
{
    const u16 *p = mfmbuf, *end = mfmbuf + MFM_WORDS - (1080 / 2) - 2;
    int found = 0;
    while (p < end) {
        if (*p != 0x4489) { p++; continue; }
        while (*p == 0x4489 && p < end) p++;
        {
            u32 info = ((rd32(p) & M) << 1) | (rd32(p + 2) & M);
            int trk = (info >> 16) & 0xff, sec = (info >> 8) & 0xff;
            u32 chk = 0, hchk, dchk;
            int i;
            const u16 *q;
            for (i = 0; i < 10; i++) chk ^= rd32(p + i * 2);
            chk &= M;
            hchk = ((rd32(p + 20) & M) << 1) | (rd32(p + 22) & M);
            if ((info >> 24) != 0xff || trk != track || sec > 10 || chk != hchk) {
                p += 2;
                continue;
            }
            dchk = ((rd32(p + 24) & M) << 1) | (rd32(p + 26) & M);
            q = p + 28;
            {
                u32 *out = (u32 *)(trackbuf + sec * 512);
                const u16 *odd = q, *even = q + 256;
                chk = 0;
                for (i = 0; i < 128; i++) {
                    u32 o = rd32(odd), e = rd32(even);
                    chk ^= o ^ e;
                    *out++ = ((o & M) << 1) | (e & M);
                    odd += 2;
                    even += 2;
                }
                chk &= M;
            }
            if (chk == dchk)
                found |= 1 << sec;
            p = q + 512;
        }
    }
    return found;
}

static void start_read(int track)
{
    drive_side(track & 1);
    DSKLEN = 0x4000;
    DMACON = DMAF_SETCLR | DMAF_MASTER | DMAF_DISK;
    ADKCON = 0x7f00;
    ADKCON = 0x9500;                   /* MFMPREC | WORDSYNC | FAST */
    DSKSYNC = 0x4489;
    DSKPT = (u32)mfmbuf;
    INTREQ = INTF_DSKBLK;
    DSKLEN = 0x8000 | MFM_WORDS;
    DSKLEN = 0x8000 | MFM_WORDS;
}

/* ---- request queue ---- */
int k_loader_queue(int disk, u32 offset, u32 len, void *dest, volatile int *done)
{
    int next = (q_tail + 1) % QSIZE;
    struct load_req *r;
    while (next == q_head)
        ;                              /* queue full: wait */
    r = &queue[q_tail];
    r->disk = disk;
    r->offset = offset;
    r->len = len;
    r->dest = (u8 *)dest;
    r->done = done;
    if (done) *done = 0;
    /* the request must be complete before the interrupt sees it */
    __asm__ volatile("" ::: "memory");
    q_tail = next;
    return 0;
}

int k_loader_busy(void) { return q_head != q_tail; }
int k_loader_current_disk(void) { return L.disk; }

/* debug: state in red, cylinder in green (low bits), errors in blue */
u32 k_loader_debug(void)
{
    return ((u32)L.state << 28) | ((u32)(L.cyl & 0xff) << 20) | ((u32)(L.want_track & 0xff) << 12) |
           ((q_head & 15) << 8) | ((q_tail & 15) << 4) | (k_loader_errors & 15);
}

/* Copies the overlap of the cached track with the current request. */
static void serve_from_cache(struct load_req *r)
{
    u32 tstart = (u32)L.cached_track * TRACK_BYTES;
    while (r->len && r->offset >= tstart && r->offset < tstart + TRACK_BYTES) {
        u32 o = r->offset - tstart;
        u32 n = TRACK_BYTES - o;
        if (n > r->len) n = r->len;
        memcpy(r->dest, trackbuf + o, n);
        r->dest += n;
        r->offset += n;
        r->len -= n;
    }
}

/* Called once per frame from the level 3 interrupt (interrupts enabled). */
void k_loader_step(void)
{
    struct load_req *r;
    if (q_head == q_tail) {
        if (L.motor && ++L.idle_frames > 100) {
            drive_motor(0);
        }
        return;
    }
    L.idle_frames = 0;
    r = &queue[q_head];

    if (k_ramdisk) {
        u32 n = r->len > RAM_CHUNK ? RAM_CHUNK : r->len;
        memcpy(r->dest, k_ramdisk + (u32)(r->disk - 1) * 901120 + r->offset, n);
        r->dest += n;
        r->offset += n;
        r->len -= n;
        L.disk = r->disk;
        if (r->len == 0) goto finished;
        return;
    }

    switch (L.state) {
    case S_IDLE:
        if (r->disk == L.disk && L.cached_track >= 0) {
            serve_from_cache(r);
            if (r->len == 0) goto finished;
        }
        if (!L.motor) {
            drive_motor(1);
            L.wait = 25;               /* 500 ms spin-up */
            L.state = S_SPINUP;
            return;
        }
        L.state = S_SPINUP;
        L.wait = 0;
        /* fall through */
    case S_SPINUP:
        if (L.wait > 0 && (CIAA_PRA & 0x20)) { L.wait--; return; }
        if (r->disk != L.disk) {
            L.want_track = 0;          /* read the bootblock to identify disk */
        } else {
            L.want_track = r->offset / TRACK_BYTES;
        }
        L.retries = 0;
        if (L.cyl < 0) { L.state = S_SEEK0; L.wait = 80; return; }
        L.state = S_STEP;
        /* fall through */
    case S_STEP:
        if (L.state == S_SEEK0) break;
        if (L.cyl != (L.want_track >> 1)) {
            int in = L.cyl < (L.want_track >> 1);
            drive_step(in);
            L.cyl += in ? 1 : -1;
            L.state = S_STEP;
            return;
        }
        L.state = S_SETTLE;
        return;
    case S_SETTLE:
        start_read(L.want_track);
        L.wait = 60;
        L.state = S_READ;
        return;
    case S_READ:
        if (!(INTREQR & INTF_DSKBLK)) {
            if (--L.wait <= 0) {       /* timeout: no disk / no sync */
                DSKLEN = 0x4000;
                k_loader_errors++;
                L.cyl = -1;
                if (r->disk != L.disk)
                    other_drive();     /* maybe the disk is in the other drive */
                L.disk = -1;
                L.state = S_IDLE;
            }
            return;
        }
        INTREQ = INTF_DSKBLK;
        DSKLEN = 0x4000;
        if (decode_track(L.want_track) != 0x7ff) {
            k_loader_errors++;
            if (++L.retries > 8) {     /* re-calibrate the head */
                L.cyl = -1;
                L.state = S_SEEK0;
                L.wait = 80;
                L.retries = 0;
                return;
            }
            L.state = S_SETTLE;
            return;
        }
        L.cached_track = L.want_track;
        if (r->disk != L.disk) {
            u32 id = *(u32 *)(trackbuf + 1020);
            if (id == DISK_ID(r->disk)) {
                L.disk = r->disk;
                k_loader_wanted_disk = 0;
            } else {
                L.disk = -1;
                L.cached_track = -1;
                k_loader_wanted_disk = r->disk;
                other_drive();
                L.state = S_IDLE;
                return;
            }
        }
        serve_from_cache(r);
        if (r->len == 0) goto finished;
        L.state = S_SPINUP;
        L.wait = 0;
        return;
    }

    if (L.state == S_SEEK0) {
        if (drive_track0()) {
            L.cyl = 0;
            L.state = S_STEP;
            return;
        }
        if (--L.wait <= 0) {           /* no track 0 signal: assume there */
            L.cyl = 0;
            L.state = S_STEP;
            return;
        }
        drive_step(0);
        return;
    }
    return;

finished:
    if (r->done) *r->done = 1;
    q_head = (q_head + 1) % QSIZE;
    L.state = S_IDLE;
}

void k_loader_init(void)
{
    CIAB_DDRB = 0xff;
    CIAB_PRB = 0xff;
    L.cyl = -1;
    L.disk = -1;
    L.cached_track = -1;
    L.state = S_IDLE;
    q_head = q_tail = 0;
}
