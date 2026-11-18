"""Minimal FLI/FLC decoder -> list of (palette, 8 bit frame bytes)."""
import struct


def read(path):
    d = open(path, 'rb').read()
    size, magic, frames, w, h = struct.unpack_from('<IHHHH', d, 0)
    pal = [(0, 0, 0)] * 256
    pix = bytearray(w * h)
    p = 128
    out = []
    while p < len(d) and len(out) < frames + 1:
        fsize, fmagic, nchunks = struct.unpack_from('<IHH', d, p)
        if fmagic != 0xF1FA:
            p += fsize
            continue
        q = p + 16
        for _ in range(nchunks):
            csize, ctype = struct.unpack_from('<IH', d, q)
            c = d[q + 6:q + csize]
            if ctype in (11, 4):     # COLOR (64 levels) / COLOR256
                k = 0
                n = struct.unpack_from('<H', c, 0)[0]
                o = 2
                for _ in range(n):
                    k += c[o]
                    cnt = c[o + 1] or 256
                    o += 2
                    for i in range(cnt):
                        r, g, b = c[o:o + 3]
                        if ctype == 11:
                            r, g, b = r * 4, g * 4, b * 4
                        pal[k] = (r, g, b)
                        k += 1
                        o += 3
            elif ctype == 15:        # BRUN
                o = 0
                for y in range(h):
                    o += 1
                    x = 0
                    while x < w:
                        n = struct.unpack_from('b', c, o)[0]
                        o += 1
                        if n > 0:
                            pix[y * w + x:y * w + x + n] = bytes([c[o]]) * n
                            o += 1
                            x += n
                        else:
                            n = -n
                            pix[y * w + x:y * w + x + n] = c[o:o + n]
                            o += n
                            x += n
            elif ctype == 12:        # LC (FLI delta)
                y0, nl = struct.unpack_from('<HH', c, 0)
                o = 4
                for y in range(y0, y0 + nl):
                    npk = c[o]
                    o += 1
                    x = 0
                    for _ in range(npk):
                        x += c[o]
                        n = struct.unpack_from('b', c, o + 1)[0]
                        o += 2
                        if n > 0:
                            pix[y * w + x:y * w + x + n] = c[o:o + n]
                            o += n
                            x += n
                        else:
                            n = -n
                            pix[y * w + x:y * w + x + n] = bytes([c[o]]) * n
                            o += 1
                            x += n
            elif ctype == 7:         # SS2 (FLC word delta)
                nl = struct.unpack_from('<H', c, 0)[0]
                o = 2
                y = 0
                for _ in range(nl):
                    while True:
                        wv = struct.unpack_from('<h', c, o)[0]
                        o += 2
                        if wv < 0:
                            if wv & 0x4000:
                                y += -wv
                            else:
                                pix[y * w + w - 1] = wv & 0xff
                        else:
                            npk = wv
                            break
                    x = 0
                    for _ in range(npk):
                        x += c[o]
                        n = struct.unpack_from('b', c, o + 1)[0]
                        o += 2
                        if n > 0:
                            pix[y * w + x:y * w + x + 2 * n] = c[o:o + 2 * n]
                            o += 2 * n
                            x += 2 * n
                        else:
                            n = -n
                            pix[y * w + x:y * w + x + 2 * n] = c[o:o + 2] * n
                            o += 2
                            x += 2 * n
                    y += 1
            elif ctype == 13:        # BLACK
                pix[:] = bytes(w * h)
            elif ctype == 16:        # COPY
                pix[:] = c[:w * h]
            q += csize
        out.append((list(pal), bytes(pix)))
        p += fsize
    return w, h, out
