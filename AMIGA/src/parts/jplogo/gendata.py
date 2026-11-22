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
# bounce: 13 widths 8 pixels apart at 100 rows; at rest: the 184 pixel
# picture at all 200 rows, packed 13 rows per group (all groups have the
# same plane stride, so one modulo per line moves every plane)
NV = 13
WID = [136 + 8 * v for v in range(NV)]
BROWS = 100
versions = [src.resize((wv, BROWS), Image.BOX) for wv in WID]
# shared palette from the base picture, colour 0 black
q = base.quantize(31, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
qp = q.getpalette()[:93]
cols = [(0, 0, 0)] + [gfxconv.amiga12(tuple(qp[i:i + 3])) for i in range(0, 93, 3)]
pimg = Image.new('P', (1, 1))
flat = [c for p in cols for c in p]
pimg.putpalette(flat + flat[:3] * (256 - len(cols)))


def rows256(im):
    qi = im.quantize(palette=pimg, dither=Image.Dither.FLOYDSTEINBERG)
    wv, hv = im.size
    data = list(qi.get_flattened_data()) if hasattr(qi, "get_flattened_data") else list(qi.getdata())
    rr = []
    for y in range(hv):
        left = (256 - wv) // 2
        rr.append([0] * left + data[y * wv:(y + 1) * wv] + [0] * (256 - left - wv))
    return rr


def group(slots):
    """slots: up to NV rows of 256 pixel indices -> 5 planes x NV x 32 bytes"""
    g = bytearray()
    for p in range(5):
        for v in range(NV):
            row = slots[v] if v < len(slots) else [0] * 256
            for xb in range(32):
                b = 0
                for k in range(8):
                    b = b * 2 + ((row[xb * 8 + k] >> p) & 1)
                g.append(b)
    return bytes(g)


bv = [rows256(im) for im in versions]
full = rows256(base)
G = [group([])]                                          # 0: empty
for y in range(BROWS):
    G.append(group([bv[v][y] for v in range(NV)]))      # 1..100: bounce rows
BEMPTY = len(G)
G.append(group([]))                                      # empty after them
FG0 = len(G)
for y0 in range(0, 200, NV):
    G.append(group(full[y0:y0 + NV]))                   # full rows
G.append(group([]))                                      # empty (bottom)
H = gfxconv.Header('jplogo_data.h')
H.array('jpic', b''.join(G))
H.array('jpal', gfxconv.pal6(cols))
H.define('NV', NV)
H.define('GROUP', NV * 32 * 5)
H.define('BROWS', BROWS)
H.define('FGROUP0', FG0)
H.define('BEGROUP', BEMPTY)
H.define('EGROUP', len(G) - 1)
v = []
for l in open(os.path.join(gfxconv.ROOT, 'JPLOGO/SIN1024.INC'), encoding='latin1'):
    l = l.strip()
    if l.lower().startswith('dw'):
        v += [int(x) for x in l[2:].split(',') if x.strip()]
H.array('jsin', v, 's16')
H.write(out)
print(len(G) * NV * 160, max(v))
