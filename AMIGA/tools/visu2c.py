"""Convert a VISU scene into C data for src/common/visu3d.c."""
import os
import visu
import visu_render as vr
import gfxconv

SCALE_SHIFT = 3


def poly_area(pts):
    a = 0
    for i in range(len(pts)):
        x1, y1 = pts[i]
        x2, y2 = pts[(i + 1) % len(pts)]
        a += x1 * y2 - x2 * y1
    return abs(a) / 2


def cluster_colors(hist, palette, n, fixed_first=()):
    """Weighted k-means on the used palette entries -> n colours (8 bit RGB, 12 bit rounded)."""
    pts = [(tuple(min(255, v * 4) for v in palette[i]), w) for i, w in hist.items() if w > 0]
    pts.sort(key=lambda t: -t[1])
    if len(pts) <= n:
        cent = [p for p, w in pts]
    else:
        # init: greedy farthest point weighted
        cent = [pts[0][0]]
        while len(cent) < n:
            best, bd = None, -1
            for p, w in pts:
                d = min(sum((a - b) ** 2 for a, b in zip(p, c)) for c in cent) * (w ** 0.5)
                if d > bd:
                    bd, best = d, p
            cent.append(best)
        for _ in range(30):
            acc = [[0, 0, 0, 0] for _ in cent]
            for p, w in pts:
                k = min(range(len(cent)), key=lambda j: sum((a - b) ** 2 for a, b in zip(p, cent[j])))
                for c in range(3):
                    acc[k][c] += p[c] * w
                acc[k][3] += w
            cent = [tuple(a[c] / a[3] for c in range(3)) if a[3] else cent[i] for i, a in enumerate(acc)]
    cent = [gfxconv.amiga12(tuple(int(round(v)) for v in c)) for c in cent]
    while len(cent) < n:
        cent.append((0, 0, 0))
    return cent


def winding_stats(s, step=5):
    """Per polygon: does the screen space winding predict visibility?
    Returns {(id(obj), polyindex): (agree, disagree)}."""
    from collections import defaultdict
    v = vr.View()
    st = defaultdict(lambda: [0, 0])
    for i, (fov, r0, on) in enumerate(s.frames()):
        if i % step:
            continue
        v.set_fov(fov)
        cam = r0[0]
        for a in range(1, s.conum):
            if not on[a]:
                continue
            o = s.objs[a]
            r = vr.object_world(r0[a], cam)
            m = r[:9]
            V = [[x + y for x, y in zip(vr.rot(m, q[:3]), r[9:12])] for q in o.verts]
            N = [vr.rot(m, n) for n in o.normals[:o.nnum1]]
            for pi, p in enumerate(o.polys):
                if p['flags'] & 2 or any(V[k][2] < 512 for k in p['verts']):
                    continue
                n = N[p['normal']]
                v0 = V[p['verts'][0]]
                vis = n[0] * v0[0] + n[1] * v0[1] + n[2] * v0[2] < 0
                pts = [((v.mulx * V[k][0]) // V[k][2], (v.muly * V[k][1]) // V[k][2]) for k in p['verts']]
                area = 0
                for k in range(len(pts)):
                    x1, y1 = pts[k]
                    x2, y2 = pts[(k + 1) % len(pts)]
                    area += x1 * y2 - x2 * y1
                if abs(area) < 4:
                    continue
                st[(id(o), pi)][0 if (area > 0) == vis else 1] += 1
    return st


def convert(base, prefix, outdir, ncolors=16, color_base=16, frame_step=4):
    s = visu.Scene(os.path.join(gfxconv.ROOT, 'MAIN/DATA', base))
    wst = winding_stats(s)
    H = gfxconv.Header(prefix + '_scene.h')
    S = SCALE_SHIFT
    uniq = []
    for o in s.objs[1:]:
        if o not in uniq:
            uniq.append(o)
    for k, o in enumerate(uniq):
        H.array('%s_o%d_v' % (prefix, k), [c >> S for v in o.verts for c in v[:3]], 's16')
        H.array('%s_o%d_n' % (prefix, k), [c for n in o.normals[:max(1, o.nnum1)] for c in n] if o.normals
                else [0, 0, -16384], 's16')
        pd = []
        offmap = {}
        inv = {v: k2 for k2, v in o.polyoff.items()}
        for i, p in enumerate(o.polys):
            assert p['normal'] < max(1, o.nnum1), (o.name, p['normal'], o.nnum1)
            offmap[inv[i]] = len(pd)
            fl = p['flags'] & 0x3f
            a, b = wst.get((id(o), i), (0, 0))
            if a + b == 0 or min(a, b) > (a + b) // 10:
                fl |= 0x40          # winding unreliable: use the 3D normal test
            elif b > a:
                fl |= 0x80          # front faces wind clockwise on screen
            pd += [p['sides'] | (fl << 8), p['color'], p['normal']] + p['verts']
        pd.append(0)
        H.array('%s_o%d_pd' % (prefix, k), pd, 'u16')
        names = []
        for li, (kind, sortv, offs) in enumerate(o.lists):
            words = [sortv, len(offs)] + [offmap[x] for x in offs]
            H.array('%s_o%d_l%d' % (prefix, k, li), words, 'u16')
            names.append('%s_o%d_l%d' % (prefix, k, li))
        H.raw('static const u16 *const %s_o%d_lists[] = { %s };\n' % (prefix, k, ', '.join(names)))
    H.raw('static const struct v3obj %s_objs[] = {\n' % prefix)
    for k, o in enumerate(uniq):
        H.raw('    { %d, %d, %d, %s_o%d_v, %s_o%d_n, %s_o%d_pd, %s_o%d_lists },\n' % (
            len(o.verts), len(o.lists), o.lists[0][1], prefix, k, prefix, k, prefix, k, prefix, k))
    H.raw('};\n')
    inst = [255] + [uniq.index(o) for o in s.objs[1:]]
    H.array('%s_inst' % prefix, inst, 'u8')
    # instance flags (U2E.C): 1 = '?_' name, always drawn first; 2 = '?s01'
    # name, drawn last during animation frames 900..1100
    fl = [0]
    for o in s.objs[1:]:
        nm = o.name
        fl.append((1 if len(nm) > 1 and nm[1] == '_' else 0) | (2 if nm[1:4] == 's01' else 0))
    H.array('%s_iflags' % prefix, fl, 'u8')
    for i, st in enumerate(s.streams):
        H.array('%s_stream%d' % (prefix, i), st, 'u8')
    H.raw('static const u8 *const %s_streams[] = { %s };\n' % (
        prefix, ', '.join('%s_stream%d' % (prefix, i) for i in range(len(s.streams)))))
    H.define('%s_NSTREAMS' % prefix.upper(), len(s.streams))
    H.define('%s_CONUM' % prefix.upper(), s.conum)
    H.define('%s_KIND' % prefix.upper(), ord(s.kind))

    # colour usage histogram from the reference renderer
    v = vr.View()
    hist = {}
    for i, (fov, r0, on) in enumerate(s.frames()):
        if i % frame_step:
            continue
        for col, pts, p in vr.render_frame(s, fov, r0, on, v):
            hist[col] = hist.get(col, 0) + poly_area(pts) + 1
    cols = cluster_colors(hist, s.palette, ncolors)
    cmap = []
    for i in range(256):
        c = tuple(min(255, x * 4) for x in s.palette[i])
        k = min(range(ncolors), key=lambda j: sum((a - b) ** 2 for a, b in zip(c, cols[j])))
        cmap.append(color_base + k)
    H.array('%s_colmap' % prefix, cmap, 'u8')
    H.array('%s_cols' % prefix, [((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4) for c in cols], 'u16')
    H.write(outdir)
    return s, cols
