#!/usr/bin/env python3
"""PLZPART: plasma sine tables (pre-arranged for the copper plasma) and
the plasma palettes; cube textures and palettes."""
import sys, os, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools'))
import gfxconv

out = sys.argv[1]
SRC = os.path.join(gfxconv.ROOT, 'PLZPART')

def inc(name, kw):
    v = []
    for line in open(os.path.join(SRC, name), encoding='latin1'):
        line = line.strip()
        if line.lower().startswith(kw):
            v += [int(x) for x in line[2:].split(',') if x.strip()]
    return v

# The original tables (PSINI.INC etc.) add 15th..21st harmonics that show
# as fine texture at 4 pixel steps; at the Amiga's coarser sampling they
# alias into noise, so the tables are rebuilt without them.
D = math.pi * 2 / 4096
psini = [int(math.sin(a * D) * 55 + math.sin(a * D * 6) * 5 + 64) for a in range(16384)]
l4 = [int((math.sin(a * D) * 55 + math.sin(a * D * 5) * 8 + 64) * 8) for a in range(8192)]
l16 = [int((math.sin(a * D) * 55 + math.sin(a * D * 4) * 5 + 64) * 16) for a in range(8192)]
ptau = [int(x) for x in open(os.path.join(SRC, 'PTAU.PRE')).read().replace('{', '').replace('}', '').replace(';', '').split(',') if x.strip()]
ptau += [0] * (256 - len(ptau))

H = gfxconv.Header('plz_data.h')
# value = P[c1 + 80x + L16[R - 8x]] + P[S2 - 8x + L4[Q + 96x]]
#   R = c2 + y + 320, S2 = c3 + 2y + 320, Q = c4 + y
# rearranged so the per-column offsets are constants:
#   term1 = P[(c1 + 10R) + A[j]],  A[j] = L16[j] - 10j,       j = R - 8x
#   term2 = P[(S2 + Q/12) + B[j]], B[j] = L4[j] - floor(j/12), j = Q + 96x
# all as byte offsets into the word table Pw = 2 * psini
H.array('pw', [2 * v for v in psini], 'u16')
H.array('ta', [2 * (l16[j] - 10 * j) for j in range(8192)], 's32')
H.array('tb', [2 * l4[j] - 2 * (j // 12) for j in range(8192)], 's16')

# palettes (PLZ.C init_plz), 6 bit; entry 0 stays black
pals = [[0] * 768 for _ in range(5)]
def fill(n, ents):
    p = pals[n]
    for i, c in enumerate(ents):
        p[3 + i * 3:6 + i * 3] = list(c)
T = ptau
fill(0, [(T[a], T[0], T[0]) for a in range(1, 64)] + [(T[63 - a], T[0], T[0]) for a in range(64)] +
     [(T[0], T[0], T[a]) for a in range(64)] + [(T[a], T[0], T[63 - a]) for a in range(64)])
fill(1, [(T[a], T[0], T[0]) for a in range(1, 64)] + [(T[63 - a], T[0], T[a]) for a in range(64)] +
     [(T[0], T[a], T[63 - a]) for a in range(64)] + [(T[a], T[63], T[a]) for a in range(64)])
fill(3, [(T[a], T[0], T[0]) for a in range(1, 64)] + [(T[63], T[a], T[a]) for a in range(64)] +
     [(T[63 - a], T[63 - a], T[63]) for a in range(64)] + [(T[0], T[0], T[63]) for a in range(64)])
fill(2, [(T[0] // 2,) * 3 for a in range(1, 64)] + [(T[a] // 2,) * 3 for a in range(64)] +
     [(T[63 - a] // 2,) * 3 for a in range(64)] + [(T[0] // 2,) * 3 for a in range(64)])
fill(4, [(T[63 - a * 64 // 75],) * 3 for a in range(1, 75)] + [(0, 0, 0)] * 106 +
     [(T[a * 64 // 75] * 8 // 10, T[a * 64 // 75] * 9 // 10, T[a * 64 // 75]) for a in range(75)])
H.array('plzpal', [v for p in pals for v in p[:768]])
H.write(out)
