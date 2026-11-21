#!/usr/bin/env python3
"""ENDSCRL: font (30 rows of 1500, 2 bits), character table, the text."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv

out = sys.argv[1]
SRC = os.path.join(gfxconv.ROOT, 'ENDSCRL')
v = []
for l in open(os.path.join(SRC, 'FONA.INC'), encoding='latin1'):
    l = l.strip()
    if l.lower().startswith('db'):
        v += [int(x) for x in l[2:].split(',') if x.strip()]
FY = 30
font = [v[y * 1500:(y + 1) * 1500] for y in range(31)]
# the character order, as bytes from MAIN.C (it has two code page 437 letters)
src = open(os.path.join(SRC, 'MAIN.C'), 'rb').read()
a = src.index(b'fonaorder="') + len(b'fonaorder="')
order = src[a:src.index(b'";', a)]
pos, wid = {}, {}
x = 0
for ch in order:
    while x < 1500 and not any(font[y][x] for y in range(FY)):
        x += 1
    b = x
    while x < 1500 and any(font[y][x] for y in range(FY)):
        x += 1
    pos[ch], wid[ch] = b, x - b
pos[32], wid[32] = 1500 - 20, 16
H = gfxconv.Header('endscrl_data.h')
H.array('efontab', [x for c in range(256) for x in (pos.get(c, 0), wid.get(c, 0))], 'u16')
H.array('efont', [min(3, font[y][x]) for y in range(FY) for x in range(1500)])
text = open(os.path.join(SRC, 'ENDSCROL.TXT'), 'rb').read().replace(b'\r', b'')
H.array('etext', list(text) + [0])
H.define('FONAY', FY)
H.write(out)
print(len(order), max(v), len(text))
