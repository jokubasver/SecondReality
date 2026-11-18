"""IFF ILBM / PBM reader -> (width, height, pixels[list of bytes rows], palette[(r,g,b) 0..255])"""
import struct

def _unpack_byterun1(data, pos, n):
    out = bytearray()
    while len(out) < n:
        c = data[pos]; pos += 1
        if c < 128:
            out += data[pos:pos + c + 1]; pos += c + 1
        elif c > 128:
            out += bytes([data[pos]]) * (257 - c); pos += 1
    return bytes(out[:n]), pos

def read(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'FORM', path
    kind = d[8:12]
    pos = 12
    w = h = nplanes = comp = 0
    pal = []
    body = None
    masking = 0
    while pos < len(d) - 8:
        cid = d[pos:pos + 4]; ln = struct.unpack('>I', d[pos + 4:pos + 8])[0]
        c = d[pos + 8:pos + 8 + ln]
        if cid == b'BMHD':
            w, h, x, y, nplanes, masking, comp = struct.unpack('>HHhhBBB', c[:11])
        elif cid == b'CMAP':
            pal = [tuple(c[i:i + 3]) for i in range(0, len(c) - 2, 3)]
        elif cid == b'BODY':
            body = c
        pos += 8 + ln + (ln & 1)
    rows = []
    p = 0
    if kind == b'PBM ':
        rw = (w + 1) & ~1
        for y in range(h):
            if comp: r, p = _unpack_byterun1(body, p, rw)
            else: r = body[p:p + rw]; p += rw
            rows.append(bytearray(r[:w]))
    else:
        bpr = ((w + 15) // 16) * 2
        np_ = nplanes + (1 if masking == 1 else 0)
        for y in range(h):
            planes = []
            for pl in range(np_):
                if comp: r, p = _unpack_byterun1(body, p, bpr)
                else: r = body[p:p + bpr]; p += bpr
                planes.append(r)
            row = bytearray(w)
            for x in range(w):
                v = 0
                for pl in range(nplanes):
                    if planes[pl][x >> 3] & (0x80 >> (x & 7)): v |= 1 << pl
                row[x] = v
            rows.append(row)
    return w, h, rows, pal

def to_png(path, out):
    from PIL import Image
    w, h, rows, pal = read(path)
    im = Image.new('P', (w, h))
    im.putdata([v for r in rows for v in r])
    flat = [c for p in pal for c in p]
    im.putpalette(flat + [0] * (768 - len(flat)))
    im.save(out)
    return w, h, len(pal)

if __name__ == '__main__':
    import sys
    print(to_png(sys.argv[1], sys.argv[2]))
