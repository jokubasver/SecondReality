#!/usr/bin/env python3
"""GLENZ: the checkered plate (4 colours + 'seen through a back face' variants)."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, visu2c
from collections import Counter

out = sys.argv[1]
d = open(os.path.join(gfxconv.ROOT, 'GLENZ/FC.UH'), 'rb').read()
pal = [tuple(d[16 + i * 3:19 + i * 3]) for i in range(16)]
pix = d[16 + 768:16 + 768 + 64000]
rows = [pix[y * 320:(y + 1) * 320] for y in range(116)]
cnt = Counter(v for r in rows for v in r)
# 3 purple clusters + black
hist = {i: n for i, n in cnt.items() if i != 0}
cols = visu2c.cluster_colors(hist, pal, 3)
q8 = [(0, 0, 0)] + cols
def nearest(i):
    if i == 0:
        return 0
    c = tuple(min(255, v * 4) for v in pal[i])
    return 1 + min(range(3), key=lambda j: sum((a - b) ** 2 for a, b in zip(c, cols[j])))
qmap = [nearest(i) for i in range(16)]
idx = [[qmap[v] for v in r] for r in rows]
pl = gfxconv.planes(idx, 2, width=320)
H = gfxconv.Header('glenz_data.h')
H.array('plate0', pl[0])
H.array('plate1', pl[1])
# 6 bit palettes: plate colour and the colour when a back face is over it (index | 4)
pp, pb = [], []
for q in range(4):
    members = [(i, n) for i, n in cnt.items() if qmap[i] == q] or [(0, 1)]
    tot = sum(n for _, n in members)
    pp += [sum(pal[i][k] * n for i, n in members) // tot for k in range(3)]
    pb += [sum(pal[i | 4][k] * n for i, n in members) // tot for k in range(3)]
H.array('plate_pal', pp)
H.array('plate_back', pb)
H.write(out)
