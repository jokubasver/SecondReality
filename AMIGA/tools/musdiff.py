#!/usr/bin/env python3
"""
Compares the 4 channel Paula conversion with the original 8 voice S3M mix.

Renders both (mono), splits them into 1/8 s frames and 8 frequency bands,
and reports the log-spectral distance per frame; prints the overall score
and the worst stretches by order/row.  Optionally writes both WAVs.

  tools/musdiff.py ../MAIN/MUSIC1.S3M --order 0 [--alloc old|new] [--budget N] [--wav out]
"""
import sys, os, argparse, wave
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from s3m import S3M
import s3m2paula as SP

RATE = 22050


def render_ref(mod, ticks, chmap):
    smps = {}
    out = []
    for mk, voices, tempo in ticks:
        n = int(round(RATE / (tempo * 2 / 5.0)))
        buf = np.zeros(n, np.float32)
        for ch in chmap:
            if ch is None:
                continue
            st = voices[ch]
            if st is None:
                continue
            smp, pos, hz, vol, trig = st
            s = mod.samples[smp - 1]
            a = smps.get(smp)
            if a is None:
                a = smps[smp] = np.frombuffer(s.pcm, dtype=np.int8).astype(np.float32)
            idx = pos + hz / RATE * np.arange(n)
            if s.loop:
                ll = s.loopend - s.loopbeg
                idx = np.where(idx >= s.loopend, s.loopbeg + (idx - s.loopbeg) % ll, idx)
            ok = idx < len(a) - 1
            i0 = np.clip(idx.astype(np.int64), 0, len(a) - 2)
            fr = idx - i0
            sig = (a[i0] * (1 - fr) + a[i0 + 1] * fr) * ok
            buf += sig * vol / 64.0
        out.append(buf)
    return np.concatenate(out) / 2.0      # 8 voices -> same scale as 4


def render_paula(alloc, table, bank):
    smps = {k: np.frombuffer(bytes(bank[t['off']:t['off'] + t['len']]), dtype=np.int8).astype(np.float32)
            for k, t in table.items()}
    nch = len(alloc[0][1])
    chpos = [0.0] * nch
    chsmp = [None] * nch
    out = []
    for mk, res, tempo in alloc:
        n = int(round(RATE / (tempo * 2 / 5.0)))
        buf = np.zeros(n, np.float32)
        for p in range(nch):
            st = res[p]
            if st is None:
                chsmp[p] = None
                continue
            smp, pos, hz, vol, trig, virt = st
            t = table[smp]
            if trig is not None or chsmp[p] != smp:
                chpos[p] = (int(trig * t['r']) & ~1) if trig is not None else pos * t['r']
                chsmp[p] = smp
            a = smps[smp]
            step = hz * t['r'] / RATE
            idx = chpos[p] + step * np.arange(n)
            if t['ll']:
                lb, ll = t['lb'], t['ll']
                idx = np.where(idx >= lb + ll, lb + (idx - lb) % ll, idx)
                ok = np.ones(n, bool)
            else:
                ok = idx < len(a)
            ii = np.clip(idx.astype(np.int64), 0, len(a) - 1)   # Paula: no interpolation
            g = t.get('g', 1.0)
            buf += np.where(ok, a[ii], 0) * vol / 64.0 / g
            chpos[p] += step * n
            if t['ll'] and chpos[p] >= t['lb'] + t['ll']:
                chpos[p] = t['lb'] + (chpos[p] - t['lb']) % t['ll']
        out.append(buf)
    return np.concatenate(out)


def frames_markers(ticks):
    """order/row at the start of each 1/8 s frame"""
    pos = []
    t = 0.0
    cur = (0, 0)
    for mk, voices, tempo in ticks:
        if mk: cur = (mk[0], mk[1])
        pos.append((t, cur))
        t += 1.0 / (tempo * 2 / 5.0)
    return pos


def band_energy(x):
    F = RATE // 8
    nf = len(x) // F
    x = x[:nf * F].reshape(nf, F) * np.hanning(F)
    S = np.abs(np.fft.rfft(x, axis=1)) ** 2
    f = np.fft.rfftfreq(F, 1.0 / RATE)
    edges = [0, 100, 200, 400, 800, 1600, 3200, 6400, 11025]
    B = np.stack([S[:, (f >= edges[i]) & (f < edges[i + 1])].sum(1) for i in range(8)], 1)
    return 10 * np.log10(B + 1e3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('s3m')
    ap.add_argument('--order', type=int, default=0)
    ap.add_argument('--alloc', default='new')
    ap.add_argument('--budget', type=int, default=240000)
    ap.add_argument('--wav')
    ap.add_argument('--seconds', type=float)
    ap.add_argument('--stereo', action='store_true', help='compare left and right separately')
    ap.add_argument('--prio', help='note priorities (as s3m2paula --prio)')
    a = ap.parse_args()
    mod = S3M(a.s3m)
    SP.alias_inverted(mod, verbose=False)
    chmap = SP.channel_map(mod)
    emu, ticks = SP.simulate(mod, a.order)
    if a.seconds:
        n = 0; t = 0.0
        while n < len(ticks) and t < a.seconds:
            t += 1.0 / (ticks[n][2] * 2 / 5.0); n += 1
        ticks = ticks[:n]
    bank, table = SP.build_samples(mod, [(emu, ticks)], a.budget, verbose=False)
    if a.alloc == 'all':
        # every voice on its own channel: the floor set by the samples alone
        al = [(mk, [(st + (None if 0 else vi,)) if st else None for vi, st in
                    ((vi, voices[chmap[vi]]) for vi in range(8))], tempo)
              for mk, voices, tempo in ticks]
        al = [(mk, [(r[0], r[1], r[2], r[3], r[4], vi) if r else None for vi, r in enumerate(res)], tempo)
              for mk, res, tempo in al]
    elif a.alloc == 'old':
        al = SP.allocate(mod, ticks, table, chmap)
    else:
        import paula_alloc
        al = paula_alloc.allocate(mod, ticks, table, chmap,
                                  paula_alloc.load_prio(a.prio) if a.prio else None)
    sides = [(None, None)]
    if a.stereo:
        # left: virtual voices 0,2,4,6 / Paula 0,3 (or ids 0,2,4,6 for 'all')
        sides = [(0, (0, 3) if a.alloc != 'all' else (0, 2, 4, 6)),
                 (1, (1, 2) if a.alloc != 'all' else (1, 3, 5, 7))]
    ds = []
    for side, chs in sides:
        cm = chmap if side is None else [c if i % 2 == side else None for i, c in enumerate(chmap)]
        als = al if side is None else [(mk, [x if i in chs else None for i, x in enumerate(res)], tp)
                                       for mk, res, tp in al]
        ref = render_ref(mod, ticks, cm)
        pau = render_paula(als, table, bank)
        n = min(len(ref), len(pau))
        ref, pau = ref[:n], pau[:n]
        if a.alloc == 'all':
            pau = pau / 2.0
        E1, E2 = band_energy(ref), band_energy(pau)
        E2 = E2 - (E2 - E1).mean(0)          # ignore a constant level difference per band
        ds.append(np.sqrt(np.mean((E1 - E2) ** 2, axis=1)))
    d = np.mean(ds, axis=0)
    print('mean distance %.2f dB, 90%% %.2f, max %.2f' % (d.mean(), np.percentile(d, 90), d.max()))
    # worst stretches (2 s windows)
    mk = frames_markers(ticks)
    w = 16
    sm = np.convolve(d, np.ones(w) / w, mode='valid')
    order = np.argsort(-sm)
    shown = []
    for i in order:
        if any(abs(i - j) < w for j in shown): continue
        shown.append(i)
        t = i / 8.0
        k = max(0, np.searchsorted([m[0] for m in mk], t) - 1)
        print('  %6.1f s  order %3d row %2d  %.2f dB' % (t, mk[k][1][0], mk[k][1][1], sm[i]))
        if len(shown) >= 6: break
    if a.wav:
        for nm, x in (('ref', ref), (a.alloc, pau)):
            pcm = np.clip(x / 128.0 * 32767 * 0.9, -32768, 32767).astype('<i2')
            wv = wave.open('%s_%s.wav' % (a.wav, nm), 'wb')
            wv.setnchannels(1); wv.setsampwidth(2); wv.setframerate(RATE)
            wv.writeframes(pcm.tobytes()); wv.close()


if __name__ == '__main__':
    main()
