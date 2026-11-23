/*
 * Software mixer: Paula channels in the stream's mixer mask play blocks
 * that the CPU mixes from 2 voices each (voices the converter could not
 * give a channel of their own).
 *
 * Each mixer channel alternates between two blocks of blen samples (about
 * 15 ms: 128 at 8287 Hz, 256 at 16574 Hz).  The
 * audio interrupt comes when Paula starts a block: the other block is
 * queued behind it and mixed (at interrupt level 2, so display and blitter
 * interrupts still come through) while this one plays.
 *
 * The music tick (level 6) does not touch the voices the mixer works on:
 * it posts changes, which are taken over at the start of each block.
 */
#include "kernel_int.h"

#define MIXBLK 256                   /* largest block */
#define NMV 8                       /* up to 4 mixer channels x 2 voices */

struct mixv {                       /* layout used by mixasm.S */
    const u8 *base;
    u32 pos;
    u32 step;
    const s8 *tab;
};

void mix2(u8 *out, int n, struct mixv *a, struct mixv *b);

struct voice {
    int on;
    const u8 *smp;                  /* sample start */
    u32 end;                        /* bytes: loop end or sample end */
    u32 lb, ll;                     /* loop start / length in bytes, ll 0 = one-shot */
    u32 ipos;                       /* integer position (bytes from smp) */
    u16 frac;
    u32 step;                       /* 16.16 */
    u8 q;                           /* volume 0..32 */
};

static struct voice mv[NMV];
static struct voice pend[NMV];
static volatile u8 pflags[NMV];
#define P_TRIG 1
#define P_STEP 2
#define P_VOL 4
#define P_STOP 8

static s8 voltab[33][256];
static u8 blk[4][2][MIXBLK];
static int blen = 128;              /* block length in use */
static u8 zero[4];
static int nmix;                    /* mixer channels */
static int mixch[4];                /* their Paula channel numbers */
static u16 mixper;
static int alt[4];
static volatile u8 todo[4];         /* block + 1 to mix, 0 none */
static volatile int mixing;
u16 k_mix_mask;

volatile u32 k_mix_lines;           /* raster lines spent mixing (statistics) */

void k_mix_init(void)
{
    int q, u;
    for (q = 0; q <= 32; q++)
        for (u = 0; u < 256; u++)
            voltab[q][u] = ((s8)u * q) >> 6;
}

/* 16.16 step for a Paula period: mixer period / period */
static u32 period_step(u16 per)
{
    u32 hi, rem;
    if (!per) return 0;
    hi = mixper / per;
    rem = mixper % per;
    return (hi << 16) | (((rem << 16) / per) & 0xffff);
}

/* --------------------------------------------- called from the music tick */
void k_mix_trigger(int v, const u8 *smp, u32 lenb, u32 lbb, u32 llb, u32 offb)
{
    struct voice *p = &pend[v];
    p->smp = smp;
    p->lb = lbb;
    p->ll = llb;
    p->end = llb ? lbb + llb : lenb;
    p->ipos = offb;
    p->frac = 0;
    pflags[v] = (pflags[v] & ~P_STOP) | P_TRIG;
}

void k_mix_period(int v, u16 per)
{
    pend[v].step = period_step(per);
    pflags[v] |= P_STEP;
}

void k_mix_volume(int v, u8 vol)
{
    pend[v].q = vol > 32 ? 32 : vol;
    pflags[v] |= P_VOL;
}

void k_mix_stop(int v)
{
    pflags[v] = (pflags[v] & ~P_TRIG) | P_STOP;
}

/* ------------------------------------------------------------- mixing */
static void take_pending(void)
{
    int v;
    u16 sr;
    __asm__ volatile("move.w %%sr,%0; ori.w #0x0700,%%sr" : "=d"(sr));
    for (v = 0; v < 2 * nmix; v++) {
        u8 f = pflags[v];
        struct voice *m = &mv[v], *p = &pend[v];
        if (!f) continue;
        if (f & P_TRIG) {
            m->smp = p->smp; m->end = p->end; m->lb = p->lb; m->ll = p->ll;
            m->ipos = p->ipos; m->frac = 0;
            m->on = m->ipos < m->end;
        }
        if (f & P_STEP) m->step = p->step;
        if (f & P_VOL) m->q = p->q;
        if (f & P_STOP) m->on = 0;
        pflags[v] = 0;
    }
    __asm__ volatile("move.w %0,%%sr" :: "d"(sr));
}

/* samples (up to n) before voice m reaches its end */
static int until_end(struct voice *m, int n)
{
    /* frames advanced in n samples, rounded up (16 bit multiply only) */
    u32 adv = (mulu16(n, m->step >> 8) >> 8) + n + 1;
    u32 left;
    if (m->ipos + adv < m->end)
        return n;
    left = ((m->end - m->ipos) << 16) - m->frac;
    return (left + m->step - 1) / m->step;
}

static void setup(struct mixv *x, struct voice *m)
{
    if (!m->on || !m->step) {
        x->base = zero;
        x->pos = 0;
        x->step = 0;
        x->tab = voltab[0];
        return;
    }
    x->base = m->smp + m->ipos;
    x->pos = (u32)m->frac << 16;
    x->step = (m->step << 16) | (m->step >> 16);
    x->tab = voltab[m->q];
}

static void advance(struct mixv *x, struct voice *m)
{
    if (!m->on || !m->step) return;
    m->ipos += x->pos & 0xffff;
    m->frac = x->pos >> 16;
    if (m->ipos >= m->end) {
        if (m->ll) {
            while (m->ipos >= m->end)
                m->ipos -= m->ll;
        } else
            m->on = 0;
    }
}

static void mix_block(int c, u8 *out)
{
#ifdef MIXBENCH
    {
        /* benchmark: inner loop only, positions not kept */
        static struct mixv xa, xb;
        struct voice *a = &mv[c * 2], *b = &mv[c * 2 + 1];
        setup(&xa, a);
        setup(&xb, b);
        xa.step &= 0xffff0000; xb.step &= 0xffff0000;
        mix2(out, blen, &xa, &xb);
        return;
    }
#endif
    struct voice *a = &mv[c * 2], *b = &mv[c * 2 + 1];
    struct mixv xa, xb;
    int n = blen;
    while (n > 0) {
        int seg = n, s;
        if (a->on && a->step) { s = until_end(a, seg); if (s < seg) seg = s; }
        if (b->on && b->step) { s = until_end(b, seg); if (s < seg) seg = s; }
        if (seg < 1) seg = 1;
        setup(&xa, a);
        setup(&xb, b);
        mix2(out, seg, &xa, &xb);
        advance(&xa, a);
        advance(&xb, b);
        out += seg;
        n -= seg;
    }
}

static inline int beamline(void)
{
    return ((VPOSR & 1) << 8) | (VHPOSR >> 8);
}

/* level 4: audio block interrupts of the mixer channels */
void k_level4(void)
{
    u16 r = INTREQR & 0x0780;
    int c, l0, l1;
    INTREQ = r;
    INTREQ = r;
    /* the mixer channels run in lockstep: the last one's interrupt (Paula
     * just started a block) queues the other block on all of them, and
     * that block is mixed now */
    if (nmix && (r & (INTF_AUD0 << mixch[nmix - 1]))) {
        for (c = 0; c < nmix; c++) {
            AUDLC(mixch[c]) = (u32)blk[c][alt[c]];
            todo[c] = alt[c] + 1;
            alt[c] ^= 1;
        }
    }
    if (mixing)
        return;
    mixing = 1;
    l0 = beamline();
    k_set_ipl(2);
    for (;;) {
        int any = 0;
        for (c = 0; c < nmix; c++) {
            int t = todo[c];
            if (t) {
                int bl = t - 1;
                todo[c] = 0;
                take_pending();
                mix_block(c, blk[c][bl]);
                any = 1;
            }
        }
        if (!any) break;
    }
    k_set_ipl(4);
    l1 = beamline();
    k_mix_lines += l1 >= l0 ? l1 - l0 : l1 + 313 - l0;
    mixing = 0;
}

/* ------------------------------------------------------ start / stop */
void k_mix_start(u16 mask, u16 period, u8 master)
{
    int c, v;
    k_mix_stop_all();
    k_mix_mask = mask;
    mixper = period;
    blen = period < 300 ? 256 : 128;
    nmix = 0;
    for (c = 0; c < 4; c++)
        if (mask & (1 << c))
            mixch[nmix++] = c;
    for (v = 0; v < NMV; v++) {
        mv[v].on = 0;
        pflags[v] = 0;
    }
    for (c = 0; c < nmix; c++) {
        int ch = mixch[c];
        memset(blk[c], 0, sizeof(blk[c]));
        alt[c] = 1;
        todo[c] = 0;
        AUDLC(ch) = (u32)blk[c][0];
        AUDLEN(ch) = blen / 2;
        AUDPER(ch) = period;
        AUDVOL(ch) = master;
    }
    if (!nmix) return;
    INTREQ = 0x0780;
    INTENA = INTF_SETCLR | (INTF_AUD0 << mixch[nmix - 1]);
    DMACON = DMAF_SETCLR | DMAF_MASTER | mask;
}

void k_mix_stop_all(void)
{
    INTENA = 0x0780;
    if (k_mix_mask)
        DMACON = k_mix_mask;
    k_mix_mask = 0;
    nmix = 0;
}

void k_mix_master(u8 master)
{
    int c;
    for (c = 0; c < nmix; c++)
        AUDVOL(mixch[c]) = master;
}
