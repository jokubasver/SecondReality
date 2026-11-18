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
    u32 magic;
    u16 nchunks;
    u16 maxchunk;
    u32 chunk[1];          /* nchunks + 1 offsets, relative to header */
};

volatile s16 k_mus_order, k_mus_row, k_mus_plusflags;
volatile s32 k_mus_frame;
volatile u8 k_mus_volume = 64;

static volatile int playing;
static const u8 *bank_pcm;
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
    INTENA = INTF_SETCLR | INTF_EXTER;
}

static void silence(void)
{
    int i;
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
    fade_speed = 0;
    k_mus_volume = 64;
    cur_master = 64;
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

void k_music_tick(void)
{
    u8 f;
    int ch;
    if (!playing)
        return;
    k_mus_frame++;
    if (cur_master != k_mus_volume) {
        cur_master = k_mus_volume;
        for (ch = 0; ch < 4; ch++)
            AUDVOL(ch) = (chvol[ch] * cur_master) >> 6;
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
        sp += 3;
    }
    for (ch = 0; ch < 4; ch++) {
        u8 cf;
        if (!(f & (1 << ch)))
            continue;
        cf = *sp++;
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
