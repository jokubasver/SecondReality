"""Reference renderer for VISU scenes (Python, mirrors the original engine)."""
import visu
from PIL import Image, ImageDraw

LIGHT = (12118, 10603, 3030)
AVISTAN = None


def avistan():
    global AVISTAN
    if AVISTAN is None:
        import re, os
        s = open(os.path.join(os.path.dirname(__file__), '../../VISU/AVISTAN.INC')).read()
        AVISTAN = [int(x) for x in re.findall(r'-?\d+', s.split('\n', 1)[1])]
    return AVISTAN


def matmul(a, b):
    """3x3 fixed point (UNIT) multiply, row-major lists of 9."""
    return [(a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c]) >> 14
            for r in range(3) for c in range(3)]


def s16(v):
    v &= 0xffff
    return v - 65536 if v > 32767 else v


def rot(m, v):
    return [(s16(m[r * 3]) * v[0] + s16(m[r * 3 + 1]) * v[1] + s16(m[r * 3 + 2]) * v[2]) >> 14 for r in range(3)]


class View:
    def __init__(self, x1=0, x2=319, y1=25, y2=174, z1=512, aspect=225):
        self.x1, self.x2, self.y1, self.y2, self.zmin = x1, x2, y1, y2, z1
        self.addx = (x1 + x2) >> 1
        self.addy = (y1 + y2) >> 1
        self.aspect = aspect

    def set_fov(self, fov):
        half = self.x2 - self.addx
        bx = fov >> 1
        bx = max(8 * 64, min(16383, bx))
        i = (bx >> 6) & ~0
        t = avistan()[i]
        self.mulx = (half * t) >> 8
        self.muly = (self.mulx * self.aspect) >> 8


def object_world(r0, cam):
    """applyrmatrix: R = cam * obj, pos = cam*pos + campos"""
    m = matmul(cam[:9], r0[:9])
    p = rot(cam[:9], r0[9:12])
    return m + [p[0] + cam[9], p[1] + cam[10], p[2] + cam[11]]


def draw_object(o, r, view, polys_out):
    """Transforms, culls, shades object o with matrix r. Appends (color, pts) to polys_out."""
    m = r[:9]
    V = [[a + b for a, b in zip(rot(m, v[:3]), r[9:12])] for v in o.verts]
    N = [rot(m, n) for n in o.normals[:o.nnum1]]
    P = []
    for x, y, z in V:
        zz = max(z, view.zmin)
        P.append(((view.mulx * x) // zz + view.addx, (view.muly * y) // zz + view.addy, z < view.zmin))
    # choose sort list: the one whose sort vertex is nearest
    best, bz = 0, None
    for i, l in enumerate(o.lists[1:], 1):
        z = V[l[1]][2]
        if bz is None or z < bz:
            bz, best = z, i
    lst = o.lists[best]
    for off in lst[2]:
        pi = o.polyoff.get(off)
        if pi is None:
            continue
        p = o.polys[pi]
        n = N[p['normal']] if p['normal'] < len(N) else (0, 0, -16384)
        v0 = V[p['verts'][0]]
        if not (p['flags'] & 0x02):
            if n[0] * v0[0] + n[1] * v0[1] + n[2] * v0[2] >= 0:
                continue
        shade = 0
        sh = p['flags'] & 0x0c
        if sh:
            d = (n[0] * LIGHT[0] + n[1] * LIGHT[1] + n[2] * LIGHT[2]) >> 16
            d = (d >> 5) + 128
            d = max(0, min(255, d))
            shade = d >> (6 - (sh >> 2))
        shade = max(1, min(30, shade))
        if any(P[i][2] for i in p['verts']):
            continue   # near clipping not handled in the reference
        polys_out.append((p['color'] + shade, [(P[i][0], P[i][1]) for i in p['verts']], p))


def render_frame(scene, fov, r0, on, view, bg=None):
    view.set_fov(fov)
    cam = r0[0]
    order = []
    for a in range(1, scene.conum):
        if on[a]:
            o = scene.objs[a]
            r = object_world(r0[a], cam)
            c = o.lists[0][1]
            z = rot(r[:9], o.verts[c][:3])[2] + r[11]
            order.append((z, a, r))
    order.sort(key=lambda t: -t[0])   # far first
    polys = []
    for z, a, r in order:
        draw_object(scene.objs[a], r, view, polys)
    return polys


def to_image(scene, polys, bg=None):
    im = Image.new('RGB', (320, 200))
    if bg is not None:
        im.paste(bg)
    d = ImageDraw.Draw(im)
    for col, pts, p in polys:
        c = scene.palette[col & 255]
        d.polygon(pts, fill=tuple(min(255, v * 4) for v in c))
    return im
