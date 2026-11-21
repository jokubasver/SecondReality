#!/usr/bin/env python3
"""MINVBALL (DOTS): sine table, reciprocal table, palette."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv

out = sys.argv[1]
v = []
for l in open(os.path.join(gfxconv.ROOT, 'DOTS/SIN1024.INC'), encoding='latin1'):
    l = l.strip()
    if l.lower().startswith('dw'):
        v += [int(x) for x in l[2:].split(',') if x.strip()]
H = gfxconv.Header('dots_data.h')
H.array('dsin', v + v[:256], 's16')
# inv[bp >> 2] = 2^22 / bp  (bp = depth, about 3000..15000)
H.array('dinv', [min(32767, (1 << 22) // max(1, i * 4 + 2)) for i in range(8192)], "s16")
H.write(out)
