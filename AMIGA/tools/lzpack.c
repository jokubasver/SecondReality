/*
 * lzpack - "LZ1" compressor (near-optimal parse) for the Amiga port.
 *
 * Grammar (bits MSB first, bytes interleaved in the same stream):
 *   stream   := litrun { match } ...
 *   litrun   := gamma(n) byte*n
 *   after a literal run:  0 -> rep match: gamma(len)
 *                         1 -> new match
 *   after a match:        0 -> litrun
 *                         1 -> new match
 *   new match := gamma(((off-1)>>8)+1) byte((off-1)&255) gamma(len-1)
 * Decoding stops when the output size (in the 4 byte header) is reached.
 *
 * usage: lzpack [-d] in out      (-d = byte delta filter before packing)
 *        lzpack -t in            (round trip self-test)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/common/unlz.h"

#define MAXOFF 65536
#define MAXLEN 65535
#define DEPTH 512

static int gbits(unsigned v)
{
    int n = 0;
    while (v >> (n + 1))
        n++;
    return 2 * n + 1;
}

typedef struct {
    unsigned cost;
    int from;      /* previous position */
    int kind;      /* 0 literal, 1 rep match, 2 new match */
    int prevstate; /* state at 'from' (0=L,1=M) */
    unsigned off;
    unsigned litlen;
} node_t;

/* bit writer */
static unsigned char *outbuf;
static size_t outpos, bitpos;
static int bitcnt;
static void putbit(int b)
{
    if (bitcnt == 0) {
        bitpos = outpos++;
        outbuf[bitpos] = 0;
        bitcnt = 8;
    }
    bitcnt--;
    if (b)
        outbuf[bitpos] |= 1 << bitcnt;
}
static void putgamma(unsigned v)
{
    int n = 0, i;
    while (v >> (n + 1))
        n++;
    for (i = 0; i < n; i++)
        putbit(0);
    putbit(1);
    for (i = n - 1; i >= 0; i--)
        putbit((v >> i) & 1);
}
static void putbyte(int b) { outbuf[outpos++] = b; }

static size_t pack(const unsigned char *in, size_t n, unsigned char *out)
{
    node_t *st[2];
    int *head, *prev;
    size_t i;
    st[0] = calloc(n + 1, sizeof(node_t));
    st[1] = calloc(n + 1, sizeof(node_t));
    head = malloc(65536 * sizeof(int));
    prev = malloc(n * sizeof(int));
    for (i = 0; i < 65536; i++)
        head[i] = -1;
    for (i = 0; i <= n; i++)
        st[0][i].cost = st[1][i].cost = 0xffffffffu;
    if (n == 0)
        return 0;
    st[0][1].cost = 9;
    st[0][1].from = 0;
    st[0][1].kind = 0;
    st[0][1].prevstate = -1;
    st[0][1].off = 1;
    st[0][1].litlen = 1;
    /* insert position 0 into hash */
    if (n > 1) {
        unsigned h = in[0] << 8 | in[1];
        prev[0] = head[h];
        head[h] = 0;
    }
    for (i = 1; i < n; i++) {
        int s;
        unsigned h;
        int cand, depth;
        unsigned bestlen = 0;
        for (s = 0; s < 2; s++) {
            node_t *cur = &st[s][i];
            if (cur->cost == 0xffffffffu)
                continue;
            /* literal */
            {
                unsigned c, ll;
                if (s == 0) {
                    ll = cur->litlen + 1;
                    c = cur->cost + 8 + gbits(ll) - gbits(cur->litlen);
                } else {
                    ll = 1;
                    c = cur->cost + 1 + 8 + 1;
                }
                if (c < st[0][i + 1].cost) {
                    node_t *d = &st[0][i + 1];
                    d->cost = c; d->from = i; d->kind = 0; d->prevstate = s;
                    d->off = cur->off; d->litlen = ll;
                }
            }
            /* rep match (only after literal) */
            if (s == 0 && cur->off <= i) {
                unsigned l = 0, o = cur->off;
                while (i + l < n && l < MAXLEN && in[i + l] == in[i + l - o])
                    l++;
                if (l) {
                    unsigned k;
                    for (k = 1; k <= l; k++) {
                        unsigned c;
                        if (k > 64 && k != l)
                            continue;
                        c = cur->cost + 1 + gbits(k);
                        if (c < st[1][i + k].cost) {
                            node_t *d = &st[1][i + k];
                            d->cost = c; d->from = i; d->kind = 1; d->prevstate = s;
                            d->off = o; d->litlen = 0;
                        }
                    }
                }
            }
        }
        /* new matches via hash chain */
        if (i + 1 < n) {
            h = in[i] << 8 | in[i + 1];
            cand = head[h];
            depth = 0;
            while (cand >= 0 && depth < DEPTH && i - cand <= MAXOFF) {
                unsigned l = 0, o = i - cand, k;
                while (i + l < n && l < MAXLEN && in[cand + l] == in[i + l])
                    l++;
                if (l >= 2 && l > bestlen) {
                    unsigned ocost = gbits(((o - 1) >> 8) + 1) + 8;
                    unsigned lo = bestlen < 2 ? 2 : bestlen + 1;
                    for (s = 0; s < 2; s++) {
                        node_t *cur = &st[s][i];
                        if (cur->cost == 0xffffffffu)
                            continue;
                        for (k = lo; k <= l; k++) {
                            unsigned c;
                            if (k > 64 && k != l)
                                continue;
                            c = cur->cost + 1 + ocost + gbits(k - 1);
                            if (c < st[1][i + k].cost) {
                                node_t *d = &st[1][i + k];
                                d->cost = c; d->from = i; d->kind = 2; d->prevstate = s;
                                d->off = o; d->litlen = 0;
                            }
                        }
                    }
                    bestlen = l;
                    if (l == MAXLEN || i + l >= n)
                        break;
                }
                cand = prev[cand];
                depth++;
            }
            prev[i] = head[h];
            head[h] = i;
        }
    }
    /* backtrack */
    {
        int s = st[0][n].cost <= st[1][n].cost ? 0 : 1;
        int pos = n, cnt = 0, k;
        int *ps = malloc((n + 1) * sizeof(int)), *ss = malloc((n + 1) * sizeof(int));
        while (pos > 0) {
            ps[cnt] = pos;
            ss[cnt] = s;
            cnt++;
            {
                node_t *d = &st[s][pos];
                int f = d->from;
                s = d->prevstate;
                pos = f;
            }
        }
        /* emit forward */
        outbuf = out;
        outpos = 4;
        bitcnt = 0;
        out[0] = (n >> 24) & 0x7f;
        out[1] = n >> 16;
        out[2] = n >> 8;
        out[3] = n;
        {
            int idx = cnt - 1;
            int prevkind = -1;
            while (idx >= 0) {
                int p = ps[idx], sk = ss[idx];
                node_t *d = &st[sk][p];
                if (d->kind == 0) {
                    /* gather literal run */
                    int start = d->from, j;
                    int end = p;
                    while (idx - 1 >= 0 && st[ss[idx - 1]][ps[idx - 1]].kind == 0) {
                        idx--;
                        end = ps[idx];
                    }
                    if (prevkind >= 1)
                        putbit(0);
                    putgamma(end - start);
                    for (j = start; j < end; j++)
                        putbyte(in[j]);
                    prevkind = 0;
                } else if (d->kind == 1) {
                    putbit(0);
                    putgamma(p - d->from);
                    prevkind = 1;
                } else {
                    putbit(1);
                    putgamma(((d->off - 1) >> 8) + 1);
                    putbyte((d->off - 1) & 255);
                    putgamma(p - d->from - 1);
                    prevkind = 2;
                }
                idx--;
            }
        }
        (void)k;
        free(ps);
        free(ss);
    }
    free(st[0]);
    free(st[1]);
    free(head);
    free(prev);
    return outpos;
}

/* Simulates decoding to find the largest (written - read) distance, which
 * determines how far from the end of the destination the packed data must be
 * placed for safe in-place unpacking. */
static long inplace_margin(const unsigned char *p, size_t psize)
{
    size_t size = unlz_size(p), w = 0;
    long maxd = 0;
    unsigned int len;
    unlz_bits_t b;
    b.src = p + 4; b.nbits = 0; b.bits = 0;
#define RD ((long)(b.src - p))
#define WR(n) do { w += (n); if ((long)w - RD > maxd) maxd = (long)w - RD; } while (0)
    if (!size) return 0;
    for (;;) {
        len = unlz_gamma(&b);
        while (len--) { b.src++; WR(1); }
        if (w >= size) break;
        if (!unlz_bit(&b)) { len = unlz_gamma(&b); goto copy; }
    newoff:
        unlz_gamma(&b); b.src++;
        len = unlz_gamma(&b) + 1;
    copy:
        while (len--) WR(1);
        if (w >= size) break;
        if (unlz_bit(&b)) goto newoff;
    }
    /* packed data starts at P = size + margin + 8 - psize; need P + r > w */
    {
        long m = maxd + (long)psize - (long)size - 8 + 4;
        return m < 0 ? 0 : m;
    }
}

static unsigned char *readfile(const char *fn, size_t *len)
{
    FILE *f = fopen(fn, "rb");
    unsigned char *b;
    if (!f) { perror(fn); exit(1); }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(*len + 1);
    if (fread(b, 1, *len, f) != *len) { perror(fn); exit(1); }
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    int delta = 0, test = 0, a = 1;
    size_t n, pn, i;
    unsigned char *in, *out, *chk, *src;
    while (a < argc && argv[a][0] == '-') {
        if (!strcmp(argv[a], "-d")) delta = 1;
        else if (!strcmp(argv[a], "-t")) test = 1;
        a++;
    }
    if (argc - a < (test ? 1 : 2)) {
        fprintf(stderr, "usage: lzpack [-d] [-t] in [out]\n");
        return 1;
    }
    in = readfile(argv[a], &n);
    src = malloc(n + 1);
    memcpy(src, in, n);
    if (delta)
        for (i = n; i-- > 1;)
            src[i] = in[i] - in[i - 1];
    out = malloc(n * 2 + 64);
    pn = pack(src, n, out);
    if (delta)
        out[0] |= 0x80;
    chk = malloc(n + 16);
    if (unlz(out, chk) != n || memcmp(chk, in, n)) {
        fprintf(stderr, "lzpack: round trip FAILED for %s\n", argv[a]);
        return 2;
    }
    if (!test) {
        FILE *f = fopen(argv[a + 1], "wb");
        fwrite(out, 1, pn, f);
        fclose(f);
    }
    fprintf(stderr, "%s: %zu -> %zu (%.1f%%)\n", argv[a], n, pn, 100.0 * pn / (n ? n : 1));
    printf("%ld\n", inplace_margin(out, pn));
    return 0;
}
