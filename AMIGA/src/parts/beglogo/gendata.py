#!/usr/bin/env python3
"""BEGLOGO: the Second Reality title picture."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, readp

out = sys.argv[1]
w, h, rows, pal = readp.read(os.path.join(gfxconv.ROOT, 'BEG/SRTITLE.UP'))
idx, qpal = gfxconv.picture(rows, pal, 320, 200, 32)
H = gfxconv.Header('beglogo_data.h')
pl = gfxconv.planes(idx, 5, width=320)
H.array('pic', b''.join(pl))
H.array('pic_pal', gfxconv.pal6(qpal))
H.write(out)
