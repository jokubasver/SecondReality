/*
 * "LZ1" decompressor, shared by the host packer (for round-trip testing)
 * and the Amiga kernel.
 *
 * Packed file layout:
 *   u32 unpacked size (big endian), bit 31 set = apply byte delta filter
 *   bitstream with interleaved bytes (see tools/lzpack.c)
 */
#ifndef UNLZ_H
#define UNLZ_H

typedef struct {
    const unsigned char *src;
    unsigned int bits;
    int nbits;
} unlz_bits_t;

static inline int unlz_bit(unlz_bits_t *b)
{
    if (b->nbits == 0) {
        b->bits = *b->src++;
        b->nbits = 8;
    }
    b->nbits--;
    return (b->bits >> b->nbits) & 1;
}

static inline unsigned int unlz_gamma(unlz_bits_t *b)
{
    int n = 0;
    unsigned int v = 1;
    while (!unlz_bit(b))
        n++;
    while (n--)
        v = (v << 1) | unlz_bit(b);
    return v;
}

static inline unsigned int unlz_size(const void *packed)
{
    const unsigned char *p = (const unsigned char *)packed;
    return (((unsigned int)p[0] & 0x7f) << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}

/* Returns the number of bytes written. */
static unsigned int unlz(const void *packed, void *dest)
{
    const unsigned char *p = (const unsigned char *)packed;
    unsigned char *d = (unsigned char *)dest;
    unsigned int size = unlz_size(p);
    int delta = p[0] & 0x80;
    unsigned char *end = d + size;
    unsigned int off = 1, len;
    unlz_bits_t b;
    b.src = p + 4;
    b.nbits = 0;
    b.bits = 0;

    if (size == 0)
        return 0;
    for (;;) {
        /* literal run */
        len = unlz_gamma(&b);
        while (len--)
            *d++ = *b.src++;
        if (d >= end)
            break;
        if (!unlz_bit(&b)) {
            /* repeat last offset */
            len = unlz_gamma(&b);
            goto copy;
        }
    newoff:
        off = ((unlz_gamma(&b) - 1) << 8) | *b.src++;
        off++;
        len = unlz_gamma(&b) + 1;
    copy:
        {
            const unsigned char *s = d - off;
            while (len--)
                *d++ = *s++;
        }
        if (d >= end)
            break;
        if (unlz_bit(&b))
            goto newoff;
    }
    if (delta) {
        unsigned char acc = 0;
        unsigned char *q = (unsigned char *)dest;
        while (q < end) {
            acc += *q;
            *q++ = acc;
        }
    }
    return size;
}

#endif
