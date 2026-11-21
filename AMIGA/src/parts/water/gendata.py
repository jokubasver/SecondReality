#!/usr/bin/env python3
"""RAYSCRL (WATER): background (16 colours), sword mask, refraction maps."""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, omf

out = sys.argv[1]
def seg(n):
    _, d, _ = omf.segments(os.path.join(gfxconv.ROOT, 'WATER/%s.OBJ' % n))
    return bytes(d[1])
bkg = seg('BKG')
mk = seg('MIEKKA')
pal = [tuple(mk[10 + i * 3:13 + i * 3]) for i in range(256)]
pix = bkg[778:778 + 64000]
rows = [pix[y * 320:(y + 1) * 320] for y in range(200)]
idx, q = gfxconv.picture(rows, pal, 320, 200, 16)
# darkest colour first (border)
d = min(range(16), key=lambda i: sum(q[i]))
perm = list(range(16))
perm[0], perm[d] = d, 0
q = [q[perm[i]] for i in range(16)]
idx = [[perm[v] for v in r] for r in idx]
H = gfxconv.Header('water_data.h')
H.array('bg', b''.join(gfxconv.planes(idx, 4, width=320)))
H.array('bgpal', gfxconv.pal6(q))
# the sword: 1 where the image has a pixel; 158 empty columns in front
font = mk[778:778 + 400 * 34]
FW = 158 + 400
mask = []
for r in range(34):
    mask += [0] * 158 + [1 if font[r * 400 + x] else 0 for x in range(400)]
H.array('swmask', mask)
H.define('FW', FW)
# average sword colour (for the tint)
cols = [pal[v] for v in font if v]
avg = [sum(c[k] for c in cols) // len(cols) for k in range(3)]
H.array('swavg', avg)
# maps: per scroll buffer pixel (r * 158 + x): count, then (byte, bit) pairs
for t in (1, 2, 3):
    b = seg('WAT%d' % t)
    w = struct.unpack('<%dH' % (len(b) // 2), b)
    i = 0
    o = []
    for k in range(158 * 34):
        c = w[i]
        i += 1
        o.append(c)
        for p in w[i:i + c]:
            o += [(p // 320) * 40 + (p % 320) // 8, 7 - (p % 8)]
        i += c
    H.array('wmap%d' % t, o, 'u16')
H.write(out)
print(avg)
