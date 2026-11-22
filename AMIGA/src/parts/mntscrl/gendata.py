#!/usr/bin/env python3
"""MNTSCRL: landscape (16 colours + highlight), text bits and the 3 mapping tables."""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv
from collections import defaultdict

out = sys.argv[1]
R = os.path.join(gfxconv.ROOT, 'FOREST')
c = open(os.path.join(R, 'HILLBACK.CLX'), 'rb').read()
pal = [tuple(c[10 + i * 3:13 + i * 3]) for i in range(256)]
pix = c[778:778 + 64000]
rows = [pix[y * 320:(y + 1) * 320] for y in range(200)]
idx, q = gfxconv.picture(rows, pal, 320, 200, 16)
order = sorted(range(16), key=lambda i: sum(q[i]))
remap = {o: n for n, o in enumerate(order)}
q = [q[o] for o in order]
idx = [[remap[v] for v in r] for r in idx]
# highlight colour per Amiga colour: average of pal[orig + 128 + 4] over its pixels
acc = defaultdict(lambda: [0, 0, 0, 0])
for y in range(200):
    for x in range(0, 320, 2):
        o = rows[y][x]
        h = pal[min(255, o + 128 + 4)]
        a = acc[idx[y][x]]
        for k in range(3): a[k] += h[k]
        a[3] += 1
hl = []
for i in range(16):
    a = acc[i]
    hl.append(tuple(a[k] // a[3] for k in range(3)) if a[3] else tuple(min(63, v // 4 + 20) for v in q[i]))
H = gfxconv.Header('mntscrl_data.h')
pl = gfxconv.planes(idx, 4, width=320)
H.array('bg', b''.join(pl))
H.array('bg_pal', gfxconv.pal6(q) + [v for c6 in hl for v in c6])
# text: 640x31, binary
d = open(os.path.join(R, 'O2.SCI'), 'rb').read()
txt = []
for x in range(640):
    col = 0
    for y in range(31):
        if d[778 + y * 640 + x] >= 1:      # the original lights every font pixel
            col |= 1 << y
    txt.append(col)
H.array('textcols', txt, 'u32', 4)
# mapping tables: per font pixel (y*237+x): count, then (byte offset << 3 | bit)
for t in range(3):
    m = open(os.path.join(R, 'POS%d.DAT' % (t + 1)), 'rb').read()
    p = 0
    words = []
    for i in range(237 * 31):
        n = struct.unpack_from('<H', m, p)[0]
        p += 2
        words.append(n)
        for k in range(n):
            o = struct.unpack_from('<H', m, p + 2 * k)[0]
            x, y = o % 320, o // 320
            words.append(((y * 40 + (x >> 3)) << 3) | (7 - (x & 7)))
        p += 2 * n
    H.array('map%d' % t, words, 'u16')
H.write(out)
