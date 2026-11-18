#!/usr/bin/env python3
"""ALKU (opening credits) data: horizon picture, intro and credit texts."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv
from PIL import Image

out = sys.argv[1]
H = gfxconv.Header('alku_data.h')

# ---- picture: 640x150 (shared with U2A)
import horizon
idx, qpal, pal = horizon.alku_horizon()
pl = gfxconv.planes(idx, 4, width=656, xoff=16)
for i in range(4):
    H.array('pic_plane%d' % i, pl[i])
H.array('pic_pal', gfxconv.pal6(qpal))

# text colours = picture palette entries 1..3 (6 bit)
tc = [pal[1], pal[2], pal[3]]
# colours 16..31: picture colour blended with full intensity text (level 3)
blend = []
for c in qpal:
    c6 = [v >> 2 for v in c]
    blend += [(tc[2][k] * 63 + c6[k] * (63 - tc[2][k])) >> 6 for k in range(3)]
H.array('pic_blend', blend)
H.array('text_cols', [0, 0, 0] + [v for c in tc for v in c])

# ---- font: replicate the glyph scanning of the original
fw, fh, frows, fpal = gfxconv.load_lbm('ALKU/FONA.LBM')
src = open(os.path.join(gfxconv.ROOT, 'ALKU/MAIN.C'), 'rb').read()
i = src.find(b'fonaorder="') + 11
order_s = src[i:src.find(b'"', i)]
fonap, fonaw = {}, {}
x = 0
for ch in order_s:
    while x < 1500 and not any(frows[y][x] & 3 for y in range(fh)): x += 1
    b = x
    while x < 1500 and any(frows[y][x] & 3 for y in range(fh)): x += 1
    fonap[ch] = b
    fonaw[ch] = x - b
fonap[32] = 1500 - 20
fonaw[32] = 16


def render(lines, rows400=400):
    """lines: (x_center, y400, text bytes). Returns 320 x rows400 level map."""
    buf = [[0] * 320 for _ in range(rows400)]
    for cx, y, t in lines:
        wsum = sum(fonaw[c] + 2 for c in t)
        x = cx - wsum // 2
        for c in t:
            for dx in range(fonaw[c]):
                for dy in range(fh):
                    v = frows[dy][fonap[c] + dx] & 3
                    if v and 0 <= x + dx < 320:
                        buf[y + dy][x + dx] |= v
            x += fonaw[c] + 2
    return buf


def halve(buf):
    """400 line level map -> 200 lines (average of line pairs, levels 0..3)."""
    return [[(a + b + 1) // 2 for a, b in zip(buf[2 * y], buf[2 * y + 1])] for y in range(len(buf) // 2)]


def crop(rows):
    nz = [y for y, r in enumerate(rows) if any(r)]
    return nz[0], rows[nz[0]:nz[-1] + 1]


# intro screens on black: 2 bitplanes (4 levels)
intro = [
    [(160, 120, b'A'), (160, 160, b'Future Crew'), (160, 200, b'Production')],
    [(160, 160, b'First Presented'), (160, 200, b'at Assembly 93')],
    [(160, 120, b'in'), (160, 160, b'\x8f'), (160, 179, b'\x99')],
]
for n, lines in enumerate(intro):
    y0, r = crop(halve(render(lines)))
    p = gfxconv.planes(r, 2, width=320)
    H.array('intro%d_p0' % n, p[0])
    H.array('intro%d_p1' % n, p[1])
    H.define('INTRO%d_Y' % n, y0)
    H.define('INTRO%d_H' % n, len(r))

# credits over the picture: 1 bitplane; tbuf row y -> screen row y+100 (400 lines)
credits = [
    [(160, 50, b'Graphics'), (160, 90, b'Marvel'), (160, 130, b'Pixel')],
    [(160, 50, b'Music'), (160, 90, b'Purple Motion'), (160, 130, b'Skaven')],
    [(160, 30, b'Code'), (160, 70, b'Psi'), (160, 110, b'Trug'), (160, 148, b'Wildfire')],
    [(160, 50, b'Additional Design'), (160, 90, b'Abyss'), (160, 130, b'Gore')],
]
for n, lines in enumerate(credits):
    full = render([(x, y + 100, t) for x, y, t in lines])
    y0, r = crop(halve(full))
    r = [[1 if v >= 2 else 0 for v in row] for row in r]
    p = gfxconv.planes(r, 1, width=320)
    H.array('cred%d' % n, p[0])
    H.define('CRED%d_Y' % n, y0 - 25)     # relative to the picture top
    H.define('CRED%d_H' % n, len(r))

H.write(out)

# preview
prev = Image.new('RGB', (640, 150))
prev.putdata([qpal[v] for r in idx for v in r])
prev.save(os.path.join(out, 'alku_preview.png'))
