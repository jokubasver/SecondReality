#!/usr/bin/env python3
"""U2A (first vector part, ships over the horizon) data."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, horizon, visu2c

out = sys.argv[1]
s, cols = visu2c.convert('U2A', 'u2a', out)

# background: the right half of the ALKU horizon, same 16 colours
idx, qpal, _ = horizon.alku_horizon()
rows = [r[320:640] for r in idx]
pl = gfxconv.planes(rows, 4, width=320)
H = gfxconv.Header('u2a_bg.h')
for i in range(4):
    H.array('bg_plane%d' % i, pl[i])
H.array('bg_pal', [((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4) for c in qpal], 'u16')
H.write(out)
