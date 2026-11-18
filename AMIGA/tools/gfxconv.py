"""Graphics conversion helpers for part data generators."""
import os, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lbm

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))


def load_lbm(rel):
    """Returns (w, h, rows of indices, palette as 8 bit RGB)."""
    return lbm.read(os.path.join(ROOT, rel))


def indexed_to_rgb(w, h, rows, pal):
    im = Image.new('RGB', (w, h))
    im.putdata([pal[v] if v < len(pal) else (0, 0, 0) for r in rows for v in r])
    return im


def amiga12(c):
    """Round an 8 bit RGB colour to the nearest 12 bit Amiga colour (as 8 bit)."""
    return tuple(min(15, (v + 8) // 17) * 17 for v in c)


def quantize(im, n, dither=True, fixed=None):
    """Quantize an RGB image to n colours with a 12 bit palette.
    Returns (2D list of indices, palette list of 8 bit RGB)."""
    if fixed is None:
        q = im.quantize(n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = q.getpalette()[:n * 3]
        pal = [amiga12(tuple(pal[i:i + 3])) for i in range(0, n * 3, 3)]
    else:
        pal = [amiga12(c) for c in fixed]
    pimg = Image.new('P', (1, 1))
    flat = [c for p in pal for c in p]
    pimg.putpalette(flat + flat[:3] * (256 - len(pal)))
    q = im.quantize(palette=pimg, dither=Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE)
    w, h = im.size
    data = list(q.getdata())
    return [data[y * w:(y + 1) * w] for y in range(h)], pal


def planes(rows, nplanes, width=None, xoff=0):
    """Convert index rows to separate bitplanes (list of bytearrays), each
    'width' pixels wide (multiple of 16) with the image placed at xoff."""
    h = len(rows)
    w = width or ((len(rows[0]) + 15) & ~15)
    bpr = w // 8
    out = [bytearray(bpr * h) for _ in range(nplanes)]
    for y, r in enumerate(rows):
        for x, v in enumerate(r):
            X = x + xoff
            if X >= w or not v:
                continue
            for p in range(nplanes):
                if v & (1 << p):
                    out[p][y * bpr + (X >> 3)] |= 0x80 >> (X & 7)
    return out


def interleave(pl, bpr):
    """Interleaved bitplanes: row0 plane0, row0 plane1, ..."""
    h = len(pl[0]) // bpr
    out = bytearray()
    for y in range(h):
        for p in pl:
            out += p[y * bpr:(y + 1) * bpr]
    return bytes(out)


def pal6(pal):
    """8 bit RGB palette -> flat list of 6 bit components (VGA style)."""
    return [v >> 2 for c in pal for v in c]


class Header:
    """Collects C arrays and writes a header for a part."""

    def __init__(self, name):
        self.name = name
        self.parts = ['/* generated */\n']

    def array(self, name, data, ctype='u8', align=2):
        vals = list(data)
        s = 'static const %s %s[%d] __attribute__((aligned(%d))) = {\n' % (ctype, name, len(vals), align)
        for i in range(0, len(vals), 24):
            s += '    ' + ','.join(str(v) for v in vals[i:i + 24]) + ',\n'
        s += '};\n'
        self.parts.append(s)

    def define(self, name, value):
        self.parts.append('#define %s %s\n' % (name, value))

    def raw(self, text):
        self.parts.append(text)

    def write(self, outdir):
        p = os.path.join(outdir, self.name)
        s = ''.join(self.parts)
        if not os.path.exists(p) or open(p).read() != s:
            open(p, 'w').write(s)


def asm_db(path):
    """Parse numbers from 'db' lines of an assembler include file."""
    out = []
    for line in open(path, 'rb').read().decode('latin1').splitlines():
        line = line.strip()
        if not line.lower().startswith('db'):
            continue
        for x in line[2:].split(','):
            x = x.strip()
            if not x:
                continue
            out.append(int(x[:-1], 16) if x.lower().endswith('h') else int(x))
    return out
