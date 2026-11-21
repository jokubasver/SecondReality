#!/usr/bin/env python3
"""JPLOGO: the picture at 7 widths (136..232 pixels), 32 colours, stored
so that the copper can pick any row of any width with a modulo jump:
row group g: for each plane: the 7 widths, 32 bytes (256 pixels) each,
the picture centred.  Group 0 and the last group are empty."""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, omf, readp
from PIL import Image

out = sys.argv[1]
_, d, _ = omf.segments(os.path.join(gfxconv.ROOT, 'JPLOGO/_PIC.OBK'))
tmp = os.path.join(out, 'jppic.bin')
open(tmp, 'wb').write(bytes(d[max(d, key=lambda k: len(d[k]))]))
w, h, rows, pal = readp.read(tmp)
pal = list(pal) + [(0, 0, 0)] * (256 - len(pal))
pal[64] = (0, 0, 0)
src = Image.new('RGB', (184, 400))
src.putdata([tuple(min(255, c * 255 // 63) for c in pal[v if v else 64]) for r in rows for v in r[70:254]])
base = src.resize((184, 200), Image.BOX)
NV = 7
WID = [136 + 16 * v for v in range(NV)]
versions = [base.resize((wv, 200), Image.BOX) for wv in WID]
# shared palette from the base picture, colour 0 black
q = base.quantize(31, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
qp = q.getpalette()[:93]
cols = [(0, 0, 0)] + [gfxconv.amiga12(tuple(qp[i:i + 3])) for i in range(0, 93, 3)]
pimg = Image.new('P', (1, 1))
flat = [c for p in cols for c in p]
pimg.putpalette(flat + flat[:3] * (256 - len(cols)))
idx = []
for im in versions:
    qi = im.quantize(palette=pimg, dither=Image.Dither.FLOYDSTEINBERG)
    wv = im.size[0]
    data = list(qi.get_flattened_data()) if hasattr(qi, "get_flattened_data") else list(qi.getdata())
    rr = []
    for y in range(200):
        left = (256 - wv) // 2
        rr.append([0] * left + data[y * wv:(y + 1) * wv] + [0] * (256 - left - wv))
    idx.append(rr)
G = []
G.append(bytes(NV * 32 * 5))
for y in range(200):
    g = bytearray()
    for p in range(5):
        for v in range(NV):
            row = idx[v][y]
            for xb in range(32):
                b = 0
                for k in range(8):
                    b = b * 2 + ((row[xb * 8 + k] >> p) & 1)
                g.append(b)
    G.append(bytes(g))
G.append(bytes(NV * 32 * 5))
H = gfxconv.Header('jplogo_data.h')
H.array('jpic', b''.join(G))
H.array('jpal', gfxconv.pal6(cols))
H.define('NV', NV)
H.define('GROUP', NV * 32 * 5)
v = []
for l in open(os.path.join(gfxconv.ROOT, 'JPLOGO/SIN1024.INC'), encoding='latin1'):
    l = l.strip()
    if l.lower().startswith('dw'):
        v += [int(x) for x in l[2:].split(',') if x.strip()]
H.array('jsin', v, 's16')
H.write(out)
print(len(G) * NV * 160, max(v))
