/*
 * Paula register-stream replayer.
 *
 * The stream (built by tools/s3m2paula.py + tools/mkstream.py) contains one
 * record per music tick.  Ticks are driven by CIA-B timer A at the module's
 * exact tempo; CIA-B timer B is used one-shot for the classic two-phase
 * DMA restart after a sample trigger.
 */
#include "kernel_int.h"

struct smp {
    u32 off;
    u16 len, lb, ll;
} __attribute__((packed));

struct stream_hdr {
    u32 magic;             /* 'SRSM' */
    u16 nchunks;
    u16 maxchunk;
    u16 mixmask;           /* Paula channels used as software mixers */
    u16 mixper;            /* their Paula period */
    u32 chunk[1];          /* nchunks + 1 offsets, relative to header */
};

volatile s16 k_mus_order, k_mus_row, k_mus_plusflags;
volatile s32 k_mus_frame;
/* row history: the tick each of the last 256 rows started on, so a part
 * that starts late can tell how far the music is past its sync point */
static u32 abs_tick;
static struct { u8 o, r; u32 t; } rowhist[256];
static u8 rowpos;

s32 k_mus_ticks_since(int o, int r)
{
    int i, p = rowpos;
    for (i = 0; i < 256; i++) {
        p = (p - 1) & 255;
        if (rowhist[p].o == o && rowhist[p].r == r && rowhist[p].t)
            return abs_tick - rowhist[p].t;
    }
    return -1;
}
volatile u8 k_mus_volume = 64;

static volatile int playing;
static const u8 *bank_pcm;
static const u8 *mix_pcm;       /* samples the mixer reads: a fast RAM copy if there is one */

/* WHDLoad: the fast pool after the disk images in ExpMem (see slave.S) */
#define FASTPOOL 0x58000
u8 *k_fast_pool(void)
{
    extern u8 *k_ramdisk;
    extern void *k_whd_resload;
    return k_whd_resload && k_ramdisk ? k_ramdisk + 2 * 901120 : 0;
}
static const struct smp *bank_tab;
static const struct stream_hdr *stream;
static const u8 *sp;            /* read pointer into the current chunk buffer */
static int cur_chunk, cur_buf;
static volatile int need_chunk = -1;   /* chunk to decode into the other buffer */
static volatile int ready_chunk = -1;  /* chunk that is decoded in the other buffer */
static int skip;
static u16 dmamask;
static int dmaphase;
static u32 loop_lc[4];
static u16 loop_len[4];
static u8 chvol[4];
static u16 per[4];
static u8 cur_master = 64;
static volatile int fade_speed, fade_acc;

/* the mixer rate: as the stream says, but 8287 Hz on machines that get the
 * 8 voice stream without being fast enough for 16574 Hz */
static u16 mix_period(void)
{
    extern int k_music8;
    if (stream->mixmask == 15 && k_music8 == 1 && stream->mixper < 428)
        return 428;
    return stream->mixper;
}

static inline u8 *chunkbuf(int i) { return (u8 *)(CHUNK_BUF + i * CHUNK_SIZE); }

static void set_tempo(u16 v)
{
    CIAB_TALO = v & 0xff;
    CIAB_TAHI = v >> 8;
}

void k_music_init(void)
{
    playing = 0;
    CIAB_CRA = 0x00;
    CIAB_CRB = 0x00;
    CIAB_ICR = 0x7f;
    (void)CIAB_ICR;
    set_tempo(14188);
    CIAB_CRA = 0x11;                  /* continuous, force load, start */
    CIAB_ICR = 0x83;                  /* enable timer A and B interrupts */
    k_mix_init();
    INTENA = INTF_SETCLR | INTF_EXTER;
}

static void silence(void)
{
    int i;
    k_mix_stop_all();
    DMACON = DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3;
    for (i = 0; i < 4; i++) {
        AUDVOL(i) = 0;
        chvol[i] = 0;
    }
}

void k_music_stop(void)
{
    playing = 0;
    silence();
}

void k_music_start(const void *bank, const void *str)
{
    const u8 *b = (const u8 *)bank;
    u16 n = *(const u16 *)b;
    u32 tab = 2 + n * 10;
    k_music_stop();
    tab = (tab + 3) & ~3u;
    bank_tab = (const struct smp *)(b + 2);
    bank_pcm = b + tab;
    mix_pcm = bank_pcm;
    stream = (const struct stream_hdr *)str;
    cur_chunk = 0;
    cur_buf = 0;
    k_unpack((const u8 *)stream + stream->chunk[0], chunkbuf(0));
    sp = chunkbuf(0);
    ready_chunk = -1;
    need_chunk = stream->nchunks > 1 ? 1 : -1;
    skip = 0;
    dmamask = 0;
    dmaphase = 0;
    k_mus_order = k_mus_row = 0;
    k_mus_plusflags = 0;
    k_mus_frame = 0;
    memset(rowhist, 0, sizeof(rowhist));
    fade_speed = 0;
    k_mus_volume = 64;
    cur_master = 64;
    {
        /* all voices mixed and WHDLoad's fast memory there: the mixer
         * reads its samples (and volume table) from a fast RAM copy; on an
         * accelerator chip RAM is behind the slow 7 MHz bus */
        u8 *pool = k_fast_pool();
        extern void k_mix_fast(u8 *pool);
        k_mix_fast(0);
        if (pool && stream->mixmask == 15) {
            u32 end = 4, i;
            for (i = 0; i < n; i++) {
                u32 e = bank_tab[i].off + (u32)bank_tab[i].len * 2;
                if (e > end) end = e;
            }
            if (0x2400 + end <= FASTPOOL) {
                memcpy(pool + 0x2400, bank_pcm, end);
                mix_pcm = pool + 0x2400;
                k_mix_fast(pool);
            }
        }
    }
    k_mix_start(stream->mixmask, mix_period(), 64);
    playing = 1;
}

void k_music_fade(int speed) { fade_acc = 0; fade_speed = speed; }

int k_mus_playing(void) { return playing; }

/* Decodes the next chunk; called from the low priority VBL section. */
void k_music_slow(void)
{
    int c = need_chunk;
    if (c >= 0 && stream) {
        k_unpack((const u8 *)stream + stream->chunk[c], chunkbuf(cur_buf ^ 1));
        ready_chunk = c;
        need_chunk = -1;
    }
    if (fade_speed && k_mus_volume) {
        fade_acc += fade_speed;
        while (fade_acc >= 16 && k_mus_volume) {
            fade_acc -= 16;
            k_mus_volume--;
        }
    }
}

static inline u16 rd16(const u8 *p) { return (p[0] << 8) | p[1]; }

static u16 mper[8];

/* one voice of a record: Paula channel 0-3, or mixed voice 4-7 */
static void voice_event(int ch)
{
    u8 cf = *sp++;
    if (ch >= 4) {
        int v = ch - 4;
        if (cf & 1) {
            const struct smp *s = &bank_tab[sp[0]];
            u16 offw = rd16(sp + 1);
            sp += 3;
            k_mix_trigger(v, mix_pcm + s->off, (u32)s->len * 2, (u32)s->lb * 2,
                          (u32)s->ll * 2, (u32)offw * 2);
        }
        if (cf & 2) {
            mper[v] += (s8)*sp++;
            k_mix_period(v, mper[v]);
        }
        if (cf & 4) {
            mper[v] = rd16(sp);
            sp += 2;
            k_mix_period(v, mper[v]);
        }
        if (cf & 8)
            k_mix_volume(v, *sp++);
        if (cf & 16)
            k_mix_stop(v);
        return;
    }
    if (cf & 1) {
        const struct smp *s = &bank_tab[sp[0]];
        u16 offw = rd16(sp + 1);
        const u8 *base = bank_pcm + s->off;
        sp += 3;
        DMACON = 1 << ch;
        AUDLC(ch) = (u32)(base + offw * 2);
        AUDLEN(ch) = s->len > offw ? s->len - offw : 1;
        if (s->ll) {
            loop_lc[ch] = (u32)(base + s->lb * 2);
            loop_len[ch] = s->ll;
        } else {
            loop_lc[ch] = (u32)bank_pcm;   /* 4 zero bytes */
            loop_len[ch] = 1;
        }
        dmamask |= 1 << ch;
    }
    if (cf & 2) {
        per[ch] += (s8)*sp++;
        AUDPER(ch) = per[ch];
    }
    if (cf & 4) {
        per[ch] = rd16(sp);
        sp += 2;
        AUDPER(ch) = per[ch];
    }
    if (cf & 8) {
        chvol[ch] = *sp++;
        AUDVOL(ch) = (chvol[ch] * cur_master) >> 6;
    }
    if (cf & 16) {
        DMACON = 1 << ch;
        AUDVOL(ch) = 0;
        chvol[ch] = 0;
    }
}

void k_music_tick(void)
{
    u8 f;
    int ch;
    if (!playing)
        return;
    k_mus_frame++;
    abs_tick++;
    if (cur_master != k_mus_volume) {
        cur_master = k_mus_volume;
        for (ch = 0; ch < 4; ch++)
            if (!(k_mix_mask & (1 << ch)))
                AUDVOL(ch) = (chvol[ch] * cur_master) >> 6;
        k_mix_master(cur_master);
    }
    if (skip) {
        skip--;
        return;
    }
again:
    f = *sp++;
    if (f & 0x80) {
        if (f == 0x81) {               /* end of chunk */
            if (ready_chunk != cur_chunk + 1) {
#ifdef MUSSTALL
                { extern volatile u32 k_dbg_val; k_dbg_val++; }
#endif
                sp--;                  /* next chunk not decoded yet: stall */
                return;
            }
            cur_chunk++;
            cur_buf ^= 1;
            sp = chunkbuf(cur_buf);
            ready_chunk = -1;
            if (cur_chunk + 1 < stream->nchunks)
                need_chunk = cur_chunk + 1;
            goto again;
        }
        playing = 0;                   /* end of song */
        silence();
        return;
    }
    if ((f & 0xc0) == 0x40) {
        skip = (f & 63) - 1;
        return;
    }
    if (f & 0x20) {
        set_tempo(rd16(sp));
        sp += 2;
    }
    if (f & 0x10) {
        k_mus_order = sp[0];
        k_mus_row = sp[1];
        k_mus_plusflags = sp[2];
        rowhist[rowpos].o = sp[0];
        rowhist[rowpos].r = sp[1];
        rowhist[rowpos].t = abs_tick;
        rowpos++;
        sp += 3;
#if defined(DEBUG_OVERLAY) && !defined(FPSSTAT) && !defined(V3PROF) && !defined(MUSSTALL)
        {
            extern volatile u32 k_dbg_val;
            k_dbg_val = ((u32)k_mus_order << 24) | ((u32)k_mus_row << 16) | ((u32)k_mus_plusflags << 8) | (k_mus_plus() & 0xff);
        }
#endif
    }
    if (f & 1) {
        u16 mask;
        int nv = 8;
        if (stream->mixmask == 15) {
            /* all 4 channels mix: 8 voices, ids 4-11 */
            mask = rd16(sp);
            sp += 2;
            nv = 12;
        } else
            mask = *sp++;
        for (ch = 0; ch < nv; ch++) {
            if (mask & (1 << ch))
                voice_event(ch);
        }
    }
    if (dmamask && !dmaphase) {
        dmaphase = 1;
        CIAB_TBLO = 0xf4;
        CIAB_TBHI = 0x01;
        CIAB_CRB = 0x19;               /* one-shot, force load, start */
    }
}

void k_music_dma(void)
{
    int ch;
    if (dmaphase == 1) {
        DMACON = DMAF_SETCLR | dmamask;
        dmaphase = 2;
        CIAB_TBLO = 0xf4;
        CIAB_TBHI = 0x01;
        CIAB_CRB = 0x19;
    } else if (dmaphase == 2) {
        for (ch = 0; ch < 4; ch++) {
            if (dmamask & (1 << ch)) {
                AUDLC(ch) = loop_lc[ch];
                AUDLEN(ch) = loop_len[ch];
            }
        }
        dmamask = 0;
        dmaphase = 0;
    }
}

/* Testing aid: runs the stream silently until the given order is reached. */
void k_music_skip_to(int order)
{
    u16 dma = 0;
    (void)dma;
    while (playing && k_mus_order < order) {
        int need = need_chunk;
        if (need >= 0)
            k_music_slow();
        k_music_tick();
        if (dmaphase) { dmaphase = 0; dmamask = 0; }
    }
    silence();
    if (stream)
        k_mix_start(stream->mixmask, mix_period(), cur_master);
}

/* nonzero once the music is at or past order o, row r: marker waits use
 * it so a part that comes a little late does not wait a whole cycle */
int k_mus_after(int o, int r)
{
    return k_mus_order > o || (k_mus_order == o && k_mus_row >= r);
}

int k_mus_plus(void)
{
    int row = k_mus_row, d;
    switch (k_mus_plusflags) {
    case 0:
        return -32;
    case 1:
        d = row - 64;
        return d < -32 ? -32 : d;
    case 2:
        return row < 32 ? row : -32;
    default:
        if (row > 32) {
            d = row - 64;
            return d < -32 ? -32 : d;
        }
        return row < 32 ? row : -32;
    }
}
