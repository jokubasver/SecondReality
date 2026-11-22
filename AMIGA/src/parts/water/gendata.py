#!/usr/bin/env python3
"""RAYSCRL (WATER): background (16 colours), the sword strip (16 colours), refraction maps."""
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
# the sword strip in its own 16 colours (palette entries 16-31): per pixel
# 0 (none) or 16 + colour; 158 empty columns in front and behind (the
# original keeps scrolling empty columns once the image has passed)
from PIL import Image
font = mk[778:778 + 400 * 34]
SW = 390
FW = 158 + SW + 160
used = sorted(set(v for v in font if v))
im = Image.new('RGB', (len(used), 1))
im.putdata([tuple(min(255, c * 4) for c in pal[v]) for v in used])
qi = im.quantize(16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
qmap = {v: qi.getpixel((i, 0)) for i, v in enumerate(used)}
qp = qi.getpalette()[:48]
swpal = [(qp[i] >> 2, qp[i + 1] >> 2, qp[i + 2] >> 2) for i in range(0, 48, 3)]
strip = []
for r in range(34):
    strip += [0] * 158 + [16 + qmap[font[r * 400 + x]] if font[r * 400 + x] else 0 for x in range(SW)] + [0] * 160
H.array('swmask', strip)
H.define('FW', FW)
open(os.path.join(out, 'water_fw.i'), 'w').write('FW = %d\n' % FW)
H.array('swpal', [c for col in swpal for c in col])
bgidx = idx
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
            # byte offset; bit | background colour << 8 (for restoring)
            o += [(p // 320) * 40 + (p % 320) // 8, (7 - (p % 8)) | (bgidx[p // 320][p % 320] << 8)]
        i += c
    H.array('wmap%d' % t, o, 'u16')
H.write(out)

