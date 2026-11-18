/* Minimal C runtime for the kernel (also used by parts). */
#include "kernel.h"

void *memcpy(void *d, const void *s, unsigned int n)
{
    u8 *dd = (u8 *)d;
    const u8 *ss = (const u8 *)s;
    if ((((u32)dd | (u32)ss) & 1) == 0) {
        u32 *dl = (u32 *)dd;
        const u32 *sl = (const u32 *)ss;
        while (n >= 16) {
            dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3];
            dl += 4; sl += 4; n -= 16;
        }
        while (n >= 4) { *dl++ = *sl++; n -= 4; }
        dd = (u8 *)dl;
        ss = (const u8 *)sl;
    }
    while (n--)
        *dd++ = *ss++;
    return d;
}

void *memmove(void *d, const void *s, unsigned int n)
{
    u8 *dd = (u8 *)d;
    const u8 *ss = (const u8 *)s;
    if (dd <= ss || dd >= ss + n)
        return memcpy(d, s, n);
    dd += n;
    ss += n;
    if ((((u32)dd | (u32)ss) & 1) == 0) {
        while (n >= 4) {
            dd -= 4; ss -= 4;
            *(u32 *)dd = *(const u32 *)ss;
            n -= 4;
        }
    }
    while (n--)
        *--dd = *--ss;
    return d;
}

void *memset(void *d, int c, unsigned int n)
{
    u8 *dd = (u8 *)d;
    u32 v = (u8)c;
    v |= v << 8;
    v |= v << 16;
    while (n && ((u32)dd & 1)) { *dd++ = c; n--; }
    {
        u32 *dl = (u32 *)dd;
        while (n >= 16) { dl[0] = v; dl[1] = v; dl[2] = v; dl[3] = v; dl += 4; n -= 16; }
        while (n >= 4) { *dl++ = v; n -= 4; }
        dd = (u8 *)dl;
    }
    while (n--)
        *dd++ = c;
    return d;
}

int memcmp(const void *a, const void *b, unsigned int n)
{
    const u8 *p = (const u8 *)a, *q = (const u8 *)b;
    while (n--) {
        if (*p != *q)
            return *p - *q;
        p++; q++;
    }
    return 0;
}
