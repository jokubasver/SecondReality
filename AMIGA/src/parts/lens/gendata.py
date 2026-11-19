#!/usr/bin/env python3
"""LENS: picture (16 colours + lens tint), lens geometry and paths."""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, omf

out = sys.argv[1]
def seg(n):
    _, d, _ = omf.segments(os.path.join(gfxconv.ROOT, 'LENS/_LENS%s.OBK' % n))
    return bytes(d[1])
exb = seg('EXB')
pal = [tuple(exb[16 + i * 3:19 + i * 3]) for i in range(256)]
pix = exb[784:784 + 64000]
rows = [pix[y * 320:(y + 1) * 320] for y in range(200)]
idx, q = gfxconv.picture(rows, pal[:64], 320, 200, 16)
H = gfxconv.Header('lens_data.h')
H.array('pic', b''.join(gfxconv.planes(idx, 4, width=320)))
# chunky copy of the picture (16 colour indices) for the lens and the zoomer
H.array('chunky', bytes(v for r in idx for v in r))
ex0 = seg('EX0')
r, g, b = ex0[4:7]
tint = [tuple(min(63, (c[k] >> 2) + (r, g, b)[k]) for k in range(3)) for c in q]
H.array('pal', gfxconv.pal6(q) + [v for c in tint for v in c])
w, h = struct.unpack_from('<hh', ex0, 0)
H.define('LENSW', w)
H.define('LENSH', h)
# lens tables: per row y: list of (screen x offset, source offset) for each layer
for n in (1, 2, 3, 4):
    H.array('lens%d' % n, struct.unpack('<%dh' % (len(seg('EX%d' % n)) // 2), seg('EX%d' % n)), 's16')
exp = seg('EXP')
a = struct.unpack_from('<h', exp, 2)[0]
p1 = struct.unpack_from('<%dh' % a, exp, 4)
p2n = (len(exp) - 4 - 2 * a) // 2
p2 = struct.unpack_from('<%dh' % p2n, exp, 4 + 2 * a)
H.array('path1', p1, 's16')
H.array('path2', p2, 's16')
H.define('PATH1N', a // 2)
H.define('PATH2N', p2n // 4)
H.write(out)
print(w, h, a // 2, p2n // 4)
