#!/usr/bin/env python3
"""TUNNELI: circle point tables and path sines from the original data files."""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv

out = sys.argv[1]
d = open(os.path.join(gfxconv.ROOT, 'TUNNELI/TUNNEL.DAT'), 'rb').read()
pts = struct.unpack('<%dh' % (138 * 64 * 2), d)
# x, y*40 per point
tab = []
for i in range(138 * 64):
    x, y = pts[i * 2], pts[i * 2 + 1]
    tab += [x, y * 40]
s = open(os.path.join(gfxconv.ROOT, 'TUNNELI/SINIT.DAT'), 'rb').read()
sinit = struct.unpack('<4097h', s[:4097 * 2])
cosit = struct.unpack('<2049h', s[4097 * 2:4097 * 2 + 2049 * 2])
H = gfxconv.Header('tunneli_data.h')
H.array('pcalc', tab, 's16')
H.array('sinit', sinit, 's16')
H.array('cosit', cosit, 's16')
H.write(out)
