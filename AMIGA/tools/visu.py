"""Reader for Future Crew VISU 3D objects (.0xx), materials (.00M) and animation streams."""
import struct


class Obj:
    pass


def load_object(path):
    d = open(path, 'rb').read()
    o = Obj()
    o.name = ''
    o.verts = []
    o.normals = []
    o.nnum1 = 0
    o.polys = []
    o.lists = []
    p = 0
    while p < len(d):
        tag = d[p:p + 4]
        ln = struct.unpack_from('<I', d, p + 4)[0]
        b = p + 8
        if tag == b'END ':
            break
        if tag == b'NAME':
            o.name = d[b:b + ln].split(b'\0')[0].decode('latin1')
        elif tag == b'VERT':
            n = struct.unpack_from('<H', d, b)[0]
            for i in range(n):
                x, y, z, nrm, _ = struct.unpack_from('<iiihh', d, b + 4 + i * 16)
                o.verts.append((x, y, z, nrm))
        elif tag == b'NORM':
            n, n1 = struct.unpack_from('<HH', d, b)
            o.nnum1 = n1
            for i in range(n):
                o.normals.append(struct.unpack_from('<hhh', d, b + 4 + i * 8))
        elif tag == b'POLY':
            q = b + 2           # leading zero word
            o.polyoff = {}
            while q < b + ln:
                off = q - b
                sides, flags, color, _ = d[q:q + 4]
                if sides == 0:
                    break
                normal = struct.unpack_from('<H', d, q + 4)[0]
                vs = list(struct.unpack_from('<%dH' % sides, d, q + 6))
                o.polyoff[off] = len(o.polys)
                o.polys.append(dict(sides=sides, flags=flags, color=color, normal=normal, verts=vs))
                q += 6 + 2 * sides
        elif tag[:3] == b'ORD':
            n = struct.unpack_from('<H', d, b)[0]
            words = struct.unpack_from('<%dH' % n, d, b)
            o.lists.append((chr(tag[3]), words[1], [w for w in words[2:] if w]))
        p = b + ln
    return o


# ---------------------------------------------------------------- scene
import os

UNIT = 16384


def s8(v):
    return v - 256 if v > 127 else v


class Scene:
    """Loads a VISU scene (name.00M, objects, name.0AA + animation streams)."""

    def __init__(self, base):
        self.base = base
        m = open(base + '.00M', 'rb').read()
        self.mat = m
        self.kind = chr(m[15])
        self.palette = [tuple(m[16 + i * 3:19 + i * 3]) for i in range(256)]
        off = struct.unpack_from('<I', m, 4)[0]
        n = struct.unpack_from('<h', m, off)[0]
        idx = struct.unpack_from('<%dh' % (n - 1), m, off + 2)
        self.conum = n
        self.objfile = [None] + list(idx)       # object 0 = camera
        cache = {}
        self.objs = [None]
        for e in idx:
            if e not in cache:
                cache[e] = load_object('%s.%03d' % (base, e))
            self.objs.append(cache[e])
        aa = open(base + '.0AA', 'rb').read()
        self.streams = []
        i = 0
        while i + 2 <= len(aa):
            a = struct.unpack_from('<h', aa, i)[0]
            if a == 0 or a == -1:
                break
            name = '%s.0%c%c' % (base, chr(a // 10 + 65), chr(a % 10 + 65))
            self.streams.append(open(name, 'rb').read())
            i += 4

    def frames(self):
        """Yields per animation frame: (fov, list of (objindex, matrix r0)) for
        enabled objects, camera matrix included as object 0."""
        n = self.conum
        r0 = [[0] * 12 for _ in range(n)]
        on = [0] * n
        si = 0
        sp = self.streams[0]
        p = 0
        while True:
            onum = 0
            while True:
                a = sp[p]; p += 1
                if a == 0xff:
                    a = sp[p]; p += 1
                    if a <= 0x7f:
                        fov = a << 8
                        break
                    elif a == 0xff:
                        si += 1
                        if si >= len(self.streams):
                            return
                        sp = self.streams[si]; p = 0
                        r0 = [[0] * 12 for _ in range(n)]
                        continue
                if (a & 0xc0) == 0xc0:
                    onum = (a & 0x3f) << 4
                    a = sp[p]; p += 1
                onum = (onum & 0xff0) | (a & 0xf)
                if (a & 0xc0) == 0x80: on[onum] = 1
                elif (a & 0xc0) == 0x40: on[onum] = 0
                r = r0[onum]
                pf = 0
                k = a & 0x30
                if k == 0x10: pf = sp[p]; p += 1
                elif k == 0x20: pf = sp[p] | sp[p + 1] << 8; p += 2
                elif k == 0x30: pf = sp[p] | sp[p + 1] << 8 | sp[p + 2] << 16; p += 3

                def lsget(f):
                    nonlocal p
                    f &= 3
                    if f == 0: return 0
                    if f == 1:
                        v = s8(sp[p]); p += 1; return v
                    if f == 2:
                        v = sp[p] | (s8(sp[p + 1]) << 8); p += 2; return v
                    v = sp[p] | sp[p + 1] << 8 | sp[p + 2] << 16 | (s8(sp[p + 3]) << 24); p += 4
                    return v
                r[9] += lsget(pf)
                r[10] += lsget(pf >> 2)
                r[11] += lsget(pf >> 4)
                if pf & 0x40:
                    for b in range(9):
                        if pf & (0x80 << b): r[b] += lsget(2)
                else:
                    for b in range(9):
                        if pf & (0x80 << b): r[b] += lsget(1)
            yield fov, [list(x) for x in r0], list(on)
