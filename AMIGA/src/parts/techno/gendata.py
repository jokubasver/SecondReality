#!/usr/bin/env python3
"""TECHNO: interference circle fields (mirrored to 640x400) and the troll picture."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, omf, readp

out = sys.argv[1]
BW = 84          # bytes per row: 16 pixel margin + 640 + 16 spare

def field(src, nplanes, interleave):
    """quadrant (320x200, rows of nplanes*40 bytes) -> planes of 672x400"""
    planes = [bytearray(BW * 400) for _ in range(nplanes)]
    rev = bytes(int('{:08b}'.format(b)[::-1], 2) for b in range(256))
    for y in range(200):
        for p in range(nplanes):
            row = src[(y * nplanes + p) * 40:(y * nplanes + p + 1) * 40] if interleave else src[y * 40:(y + 1) * 40]
            mirrored = bytes(rev[b] for b in reversed(row))
            for yy in (y, 399 - y):
                o = yy * BW + 2
                planes[p][o:o + 40] = row
                planes[p][o + 40:o + 80] = mirrored
    return planes

_, d, _ = omf.segments(os.path.join(gfxconv.ROOT, 'TECHNO/_CIRCLE.OBK'))
A = field(bytes(d[1]), 3, True)
_, d, _ = omf.segments(os.path.join(gfxconv.ROOT, 'TECHNO/_CIRCLE2.OBK'))
B = field(bytes(d[1]), 1, False)
H = gfxconv.Header('techno_data.h')
H.array('circ_a', b''.join(A))
H.array('circ_b', B[0])
w, h, rows, pal = readp.read(os.path.join(gfxconv.ROOT, 'MAIN/DATA/TROLL.UP'))
idx, qpal = gfxconv.picture(rows, pal, 320, 200, 32)
H.array('troll', b''.join(gfxconv.planes(idx, 5, width=320)))
H.array('troll_pal', gfxconv.pal6(qpal))
H.write(out)
