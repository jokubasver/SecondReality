#!/usr/bin/env python3
"""ENDLOGO: the Future Crew rings picture, 320x200, 32 colours."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, omf, readp

out = sys.argv[1]
_, d, _ = omf.segments(os.path.join(gfxconv.ROOT, 'END/_PIC.OBK'))
tmp = os.path.join(out, 'endpic.bin')
open(tmp, 'wb').write(bytes(d[max(d, key=lambda k: len(d[k]))]))
w, h, rows, pal = readp.read(tmp)
pal = list(pal) + [(0, 0, 0)] * (256 - len(pal))
idx, q = gfxconv.picture(rows, pal, 320, 200, 32)
d0 = min(range(32), key=lambda i: sum(q[i]))
perm = list(range(32))
perm[0], perm[d0] = d0, 0
q = [q[perm[i]] for i in range(32)]
idx = [[perm[v] for v in r] for r in idx]
H = gfxconv.Header('endlogo_data.h')
H.array('epic', b''.join(gfxconv.planes(idx, 5, width=320)))
H.array('epal', gfxconv.pal6(q))
H.write(out)
