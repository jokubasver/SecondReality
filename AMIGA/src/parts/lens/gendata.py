#!/usr/bin/env python3
"""LENS/ZOOM: picture (16 colours + lens tint), 2x2 lens cell map and paths."""
import sys, os, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv, omf

out = sys.argv[1]
def seg(n):
    _, d, _ = omf.segments(os.path.join(gfxconv.ROOT, 'LENS/_LENS%s.OBK' % n))
    return bytes(d[1])
def words(n):
    b = seg(n)
    return struct.unpack('<%dh' % (len(b) // 2), b)

PAD = 12                       # chunky padding rows above and below
exb = seg('EXB')
pal = [tuple(exb[16 + i * 3:19 + i * 3]) for i in range(256)]
pix = exb[784:784 + 64000]
rows = [pix[y * 320:(y + 1) * 320] for y in range(200)]
idx, q = gfxconv.picture(rows, pal[:64], 320, 200, 16)
# the darkest colour goes to index 0 (it is also the border colour)
d = min(range(16), key=lambda i: sum(q[i]))
perm = list(range(16))
perm[0], perm[d] = d, 0
q = [q[perm[i]] for i in range(16)]
idx = [[perm[v] for v in r] for r in idx]
H = gfxconv.Header('lens_data.h')
H.define('PAD', PAD)
H.array('pic', b''.join(gfxconv.planes(idx, 4, width=320)))
# chunky picture, values c*4, PAD rows of colour 0 around it
ch = bytes(PAD * 320) + bytes(v * 4 for r in idx for v in r) + bytes(PAD * 320)
H.array('chunky', ch)
ex0 = seg('EX0')
r, g, b = ex0[4:7]
tint = [tuple(min(63, (c[k] >> 2) + (r, g, b)[k]) for k in range(3)) for c in q]
H.array('pal', gfxconv.pal6(q) + [v for c in tint for v in c])

# ---- lens: simulate the original row tables -> per pixel (source, group)
LW, LH = struct.unpack_from('<hh', ex0, 0)
M = {}
for n, kind, grp in ((1, 'row', 1), (2, 'pair', 2), (3, 'pair', 3), (4, 'rest', 0)):
    t = words('EX%d' % n)
    for y in range(LH):
        p, c = t[y * 2] // 2, t[y * 2 + 1]
        if not c or (n == 1 and c < 4):
            continue
        s = t[p]
        q0 = p + 1
        for k in range(c):
            if kind == 'row':
                d, sr = s + k, s + t[q0 + k]
            elif kind == 'pair':
                d, sr = s + t[q0 + 2 * k], s + t[q0 + 2 * k + 1]
            else:
                d = sr = s + t[q0 + k]
            M[(y, d)] = (sr + y * 320, grp)
CW, CH = 80, (LH + 1) // 2     # cells per row (padded to 10 words), cell rows
center = (LH // 2) * 320 + LW // 2
cells = []
for cy in range(CH):
    for cx in range(CW):
        src = None
        for (yy, xx) in ((0, 0), (0, 1), (1, 0), (1, 1)):
            v = M.get((cy * 2 + yy, cx * 2 + xx))
            if v and v[1]:
                src = v[0]
                break
        if src is None:
            src = cy * 2 * 320 + cx * 2
        cells.append(src - center)
# unrolled gather + C2P code, one subroutine per cell row (68000 opcodes):
#   move.b d16(a0),d1 ; move.l/or.l d8(a2,d1.w),d0 (x4) ; movep.l d0,d16(a3)
# only the 8 cell groups that touch the lens are generated
def masked(y, x):
    return 0 <= y < LH and (M.get((y, x)) or (0, 0))[1] != 0
code, rowoff = [], []
for cy in range(CH):
    rowoff.append(len(code) * 2)
    for g in range(10):
        if not any(masked(cy * 2 + yy, x) for yy in (0, 1) for x in range(g * 16, g * 16 + 16)):
            continue
        for half in (0, 1):
            for j in range(4):
                c = cells[cy * CW + g * 8 + half * 4 + j]
                code += [0x1228, c & 0xffff]
                code += [0x2032 if j == 0 else 0x80b2, 0x1000 | ((-128 + 64 * j) & 0xff)]
            code += [0x01cb, g * 8 + half]
    code.append(0x4e75)
H.array('lcode', code, 'u16')
H.array('lrows', rowoff, 'u16')
# 1x1 lens mask: 11 words per row (last one empty)
mask = []
for y in range(LH):
    bits = [1 if (M.get((y, x)) or (0, 0))[1] else 0 for x in range(176)]
    for w in range(11):
        v = 0
        for i in range(16):
            v = v * 2 + bits[w * 16 + i]
        mask.append(v)
H.array('lmask', mask, 'u16')
H.define('LENSW', LW)
H.define('LENSH', LH)
H.define('CROWS', CH)

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
