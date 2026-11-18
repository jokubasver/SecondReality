#!/usr/bin/env python3
"""PAM (planet explosion): FLI frames -> 5 plane row bands over the horizon."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, horizon, fli, visu2c
from PIL import Image

out = sys.argv[1]
w, h, frames = fli.read(os.path.join(gfxconv.ROOT, 'PAM/PRAX4.FLI'))
bgfli = frames[0][1]
idx, bgpal, _ = horizon.alku_horizon()
bgrows = [[0] * 320 for _ in range(200)]
for y in range(150):
    bgrows[y + 25] = idx[y][320:640]

# explosion colours: cluster all pixels that differ from the background
hist = {}
for k in range(1, 41):
    pal, pix = frames[k]
    for i in range(0, w * h, 3):
        if pix[i] != bgfli[i]:
            c = pal[pix[i]]
            hist[c] = hist.get(c, 0) + 1
expal = [(0, 0, 0)] + visu2c.cluster_colors({i: wgt for i, (c, wgt) in enumerate(hist.items())},
                                           [tuple(v >> 2 for v in c) for c in hist], 15)
allpal = list(bgpal) + list(expal)

def nearest(c, cache={}):
    """explosion pixel -> explosion colour 1..15 (0 = background shows)"""
    if c not in cache:
        cache[c] = min(range(1, 16), key=lambda j: sum((a - b) ** 2 for a, b in zip(c, expal[j])))
    return cache[c]

H = gfxconv.Header('pam_data.h')
blob = bytearray()
index = []
for k in range(1, 41):
    pal, pix = frames[k]
    rows = [y for y in range(h) if pix[y * w:(y + 1) * w] != bgfli[y * w:(y + 1) * w]]
    r0, r1 = (rows[0], rows[-1] + 1) if rows else (0, 0)
    band = []
    for y in range(r0, r1):
        row = []
        for x in range(w):
            if pix[y * w + x] == bgfli[y * w + x]:
                row.append(0)
            else:
                row.append(nearest(pal[pix[y * w + x]]))
        band.append(row)
    pl = gfxconv.planes(band, 4, width=320) if band else [b''] * 4
    index.append((len(blob), r0, r1 - r0))
    for p in pl:
        blob += p
# pack every frame separately (decoded progressively while playing)
import subprocess, tempfile
packed = bytearray()
pindex = []
for k, (off, r0, n) in enumerate(index):
    raw = bytes(blob[off:off + n * 40 * 4])
    if not raw:
        pindex.append((len(packed), off, r0, 0))
        continue
    with tempfile.TemporaryDirectory() as td:
        fi, fo = os.path.join(td, 'i'), os.path.join(td, 'o')
        open(fi, 'wb').write(raw)
        subprocess.run([os.path.join(gfxconv.ROOT, 'AMIGA/tools/lzpack'), fi, fo], check=True, capture_output=True)
        p = open(fo, 'rb').read()
    if len(p) & 1: p += b'\0'
    pindex.append((len(packed), off, r0, n))
    packed += p
H.array('pam_packed', packed)
H.array('pam_index', [v for t in pindex for v in (t[0] >> 16, t[0] & 0xffff, t[1] >> 16, t[1] & 0xffff, t[2], t[3])], 'u16')
H.define('PAM_BANDS_SIZE', len(blob))
print('packed frames', len(packed))
pl = gfxconv.planes(bgrows, 4, width=320)
for i in range(4):
    H.array('pam_bg%d' % i, pl[i])
H.array('pam_pal', gfxconv.pal6(allpal))
H.write(out)
print('bands', len(blob))
