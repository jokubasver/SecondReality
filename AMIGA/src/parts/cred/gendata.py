#!/usr/bin/env python3
"""CRED: the 21 credit pictures (160x100, 16 colours each), the font
(halved to 16 rows, values 0..9) with its character table."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, lbm
from PIL import Image

out = sys.argv[1]
SRC = os.path.join(gfxconv.ROOT, 'CREDITS')
names = ['01', '02', '03', '04', '05', '05B', '06', '07', '08', '09', '10', '10B', '11', '12',
         '13', '14', '14B', '15', '16', '17', '18']
H = gfxconv.Header('cred_data.h')
pics, pals = [], []
for n in names:
    w, h, rows, pal = lbm.read(os.path.join(SRC, 'PICS/PIC%s.LBM' % n))
    pal = [tuple(c) for c in pal] + [(0, 0, 0)] * (256 - len(pal))
    pal6 = [tuple(v >> 2 for v in c) for c in pal]
    idx, q = gfxconv.picture(rows, pal6, 160, 100, 16)
    d0 = min(range(16), key=lambda i: sum(q[i]))
    perm = list(range(16))
    perm[0], perm[d0] = d0, 0
    q = [q[perm[i]] for i in range(16)]
    idx = [[perm[v] for v in r] for r in idx]
    pics.append(b''.join(gfxconv.planes(idx, 4, width=160)))
    pals += gfxconv.pal6(q)
H.array('cpics', b''.join(pics))
H.array('cpals', pals)
H.define('NPICS', len(names))
# font: FONA.INC, 32 rows of 1500
v = []
for l in open(os.path.join(SRC, 'FONA.INC'), encoding='latin1'):
    l = l.strip()
    if l.lower().startswith('db'):
        v += [int(x) for x in l[2:].split(',') if x.strip()]
FY = 32
font = [v[y * 1500:(y + 1) * 1500] for y in range(FY)]
order = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/?!:,.\"()+-'
pos, wid = {}, {}
x = 0
for ch in order:
    while x < 1500 and not any(font[y][x] for y in range(FY)):
        x += 1
    b = x
    while x < 1500 and any(font[y][x] for y in range(FY)):
        x += 1
    pos[ch], wid[ch] = b, x - b
pos[' '], wid[' '] = 1500 - 32, 8
tab = []
for c in range(128):
    ch = chr(c)
    tab += [pos.get(ch, 0), wid.get(ch, 0)]
H.array('cfontab', tab, 'u16')
# halve the height: brighter of each row pair
half = []
for y in range(16):
    half += [max(font[2 * y][x], font[2 * y + 1][x]) for x in range(1500)]
H.array('cfont', half)
H.write(out)
