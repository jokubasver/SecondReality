#!/usr/bin/env python3
"""
Derives note priorities for the MUSIC1 conversion from Estrayk's unfinished
4 channel Protracker arrangement of the tune ("second reality 6.mod",
modland.com/pub/modules/Protracker/Estrayk/).

The MOD's orders are aligned with the S3M's (by which samples play on which
rows); for each aligned pattern, the S3M notes that the arrangement kept are
recorded.  Writes JSON: covered/kept note lists [order, row, channel] and
per channel / per sample keep rates for the rest of the song.

  tools/expert_prio.py "second reality 6.mod" ../MAIN/MUSIC1.S3M tools/mus1_prio.json
"""
import sys, os, struct, json
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from s3m import S3M


def load_mod(path):
    d = open(path, 'rb').read()
    smp = []
    for i in range(31):
        o = 20 + i * 30
        ln = struct.unpack('>H', d[o + 22:o + 24])[0] * 2
        rs, rl = struct.unpack('>HH', d[o + 26:o + 30])
        smp.append(dict(len=ln, ls=rs * 2, ll=rl * 2))
    n = d[950]
    order = list(d[952:952 + n])
    npat = max(d[952:1080]) + 1
    pats = []
    p = 1084
    for _ in range(npat):
        rows = []
        for r in range(64):
            row = []
            for c in range(4):
                b = d[p:p + 4]; p += 4
                row.append(((b[0] & 0xf0) | (b[2] >> 4), ((b[0] & 15) << 8) | b[1]))
            rows.append(row)
        pats.append(rows)
    return smp, order, pats


def sample_map(msmp, s3m):
    """MOD sample -> S3M samples: same length, or (for the ones the arrangement
    resampled or replaced) the S3M sample they stand in for, found by usage."""
    m = {}
    for i, ms in enumerate(msmp):
        if ms['len'] < 4:
            continue
        m[i + 1] = [j + 1 for j, ss in enumerate(s3m.samples) if ss.length and abs(ss.length - ms['len']) <= 2]
    return m


def rows_mod(p, smap):
    out = []
    last = [0] * 4
    for row in p:
        st = set()
        for c, (sm, per) in enumerate(row):
            if sm: last[c] = sm
            if per and last[c]: st |= set(smap.get(last[c], []))
        out.append(st)
    return out


def rows_s3m(p):
    return [set(c[1] for c in row[:8] if c and c[0] is not None and c[0] < 254 and c[1]) for row in p]


def sim(a, b):
    sc = n = 0
    for x, y in zip(a, b):
        if x or y:
            n += 1
            sc += len(x & y) / len(x | y)
    return sc / max(n, 1)


def main():
    modp, s3mp, out = sys.argv[1:4]
    msmp, morder, mpats = load_mod(modp)
    s = S3M(s3mp)
    smap = sample_map(msmp, s)
    # stand-ins found by listening/usage: 'wavestring' (half rate copy of
    # sample 5) and the synth set 35-44
    for i, ms in enumerate(msmp):
        if not smap.get(i + 1) and ms['len'] >= 4:
            if ms['ll'] > 2 and 2900 < ms['ll'] < 3100:
                smap[i + 1] = [5]
            else:
                smap[i + 1] = list(range(35, 45))
    S = [(i, o) for i, o in enumerate(s.orders) if o < 254]
    mr = [rows_mod(mpats[o], smap) for o in morder]
    sr = [rows_s3m(s.patterns[o]) for _, o in S]
    NI, NJ = len(morder), len(S)
    dp = np.full((NI + 1, NJ + 1), -1e9); dp[0, :] = 0
    bt = {}
    for i in range(1, NI + 1):
        for j in range(NJ + 1):
            best = (-1e9, None)
            if j > 0: best = max(best, (dp[i - 1, j - 1] + sim(mr[i - 1], sr[j - 1]), (i - 1, j - 1, 1)))
            best = max(best, (dp[i - 1, j] - 0.15, (i - 1, j, 0)))
            if j > 0: best = max(best, (dp[i, j - 1] - 0.05, (i, j - 1, 0)))
            dp[i, j], bt[(i, j)] = best
    i, j = NI, int(np.argmax(dp[NI]))
    pairs = []
    while i > 0:
        pi, pj, k = bt[(i, j)]
        if k: pairs.append((i - 1, j - 1))
        i, j = pi, pj
    covered, kept = [], []
    for a, b in pairs:
        if sim(mr[a], sr[b]) < 0.5:
            continue
        sp, mp = s.patterns[S[b][1]], mpats[morder[a]]
        last = [0] * 4
        for r in range(64):
            ks = []
            for c in range(4):
                sm, per = mp[r][c]
                if sm: last[c] = sm
                if per and last[c]: ks.append(set(smap.get(last[c], [])))
            for ch in range(8):
                cell = sp[r][ch]
                if cell and cell[0] is not None and cell[0] < 254 and cell[1]:
                    covered.append([S[b][0], r, ch])
                    for k in ks:
                        if cell[1] in k:
                            kept.append([S[b][0], r, ch]); ks.remove(k); break
    kset = set(map(tuple, kept))
    base = len(kept) / len(covered)
    rate = {'ch': {}, 'smp': {}}
    for key, f in (('ch', lambda c: c[2]), ('smp', lambda c: s.patterns[s.orders[c[0]]][c[1]][c[2]][1])):
        cnt = {}
        for c in covered:
            x = f(c); t = cnt.setdefault(x, [0, 0]); t[0] += 1; t[1] += tuple(c) in kset
        for x, (t, k) in cnt.items():
            rate[key][str(x)] = ((k + 2 * base) / (t + 2)) / base     # smoothed, relative
    json.dump(dict(covered=covered, kept=kept, rate=rate), open(out, 'w'))
    print('%d of %d covered notes kept (%d aligned patterns)' % (len(kept), len(covered), len(pairs)))


if __name__ == '__main__':
    main()
