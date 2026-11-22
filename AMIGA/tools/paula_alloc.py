"""
Note-level allocation of 8 virtual S3M voices onto Paula's 4 channels.

The tick-by-tick "two loudest voices per side" choice cuts notes and
flips channels between voices.  This works on whole notes instead, with
lookahead (the whole song is known in advance):

  - a note is a run of ticks of one virtual voice from a trigger until the
    next trigger or the end of the sound;
  - its value at a tick is its audible loudness (volume x the sample's RMS
    envelope at the play position) summed over a short window ahead;
  - a new note takes a free channel (its own stereo side first), else it
    replaces the channel whose note is worth the least from now on, if it
    is worth clearly more; a note that got no channel can still start late
    (from its current position) when a channel frees up while it is loud.
"""
import numpy as np

LEFT_CH, RIGHT_CH = (0, 3), (1, 2)
WINDOW = 24          # ticks of lookahead (~0.45 s at 52 Hz)
ONSET_BONUS = 1.6    # attacks matter more than tails
STAY_BONUS = 1.25    # hysteresis against swapping
CROSS_SIDE = 0.6     # value factor for playing a voice on the other side
QUIET = 0.6          # loudness below which a channel counts as free


ECHO = 0.45          # value factor for echo/doubling voices
ECHO_TICKS = 20      # how far back an echo's original may start


def mark_echoes(notes):
    """A note is an echo if another voice started the same sample at the
    same pitch shortly before (or at the same tick, on a lower voice) at
    least as loud."""
    by = {}
    for n in notes:
        by.setdefault(n['smp'], []).append(n)
    for lst in by.values():
        lst.sort(key=lambda n: (n['start'], n['virt']))
        for i, n in enumerate(lst):
            n['echo'] = False
            for m in reversed(lst[:i]):
                if n['start'] - m['start'] > ECHO_TICKS:
                    break
                if m['virt'] != n['virt'] and m['vol0'] >= n['vol0'] and n['hz0'] > 0 and \
                        abs(m['hz0'] / n['hz0'] - 1) < 0.01:
                    n['echo'] = True
                    break


class Env:
    """RMS envelope of each sample, 128 frame resolution."""

    def __init__(self, mod):
        self.mod = mod
        self.cache = {}

    def __call__(self, smp, pos):
        e = self.cache.get(smp)
        if e is None:
            a = np.frombuffer(self.mod.samples[smp - 1].pcm, dtype=np.int8).astype(np.float64)
            n = max(1, (len(a) + 127) // 128)
            e = np.array([np.sqrt(np.mean(a[i * 128:(i + 1) * 128] ** 2)) if len(a[i * 128:(i + 1) * 128]) else 0
                          for i in range(n)])
            self.cache[smp] = e
        i = int(pos) // 128
        return e[min(i, len(e) - 1)]


def note_weight(n, prio, mod):
    """priority multiplier from the reference arrangement (see expert_prio.py)"""
    if prio is None or n['at'] is None:
        return ECHO if n['echo'] else 1.0
    key = (n['at'][0], n['at'][1], n['ch'])
    if key in prio['covered']:
        return KEEP_W if key in prio['kept'] else DROP_W
    w = prio['rate']['ch'].get(str(n['ch']), 1.0) * prio['rate']['smp'].get(str(n['smp']), 1.0)
    w = min(3.0, max(0.3, w))
    return w * (ECHO if n['echo'] else 1.0)


KEEP_W, DROP_W = 3.0, 0.3


def load_prio(path):
    import json
    d = json.load(open(path))
    return dict(covered=set(map(tuple, d['covered'])), kept=set(map(tuple, d['kept'])), rate=d['rate'])


def extract_notes(mod, ticks, chmap, prio=None):
    """Returns notes: dicts with virt, start, end (exclusive), loud[] per tick."""
    env = Env(mod)
    notes = []
    at = []
    cur_at = None
    for mk, voices, tempo in ticks:
        if mk: cur_at = (mk[0], mk[1])
        at.append(cur_at)
    for vi in range(8):
        ch = chmap[vi]
        if ch is None:
            continue
        cur = None
        for t, (mk, voices, tempo) in enumerate(ticks):
            st = voices[ch]
            if st is None:
                if cur:
                    cur['end'] = t
                    notes.append(cur)
                    cur = None
                continue
            smp, pos, hz, vol, trig = st
            new = cur is None or trig is not None or smp != cur['smp']
            if new:
                if cur:
                    cur['end'] = t
                    notes.append(cur)
                cur = dict(virt=vi, ch=ch, smp=smp, start=t, loud=[], hz0=hz, vol0=vol, at=at[t])
            cur['loud'].append(vol / 64.0 * (env(smp, pos) + 1.0))
        if cur:
            cur['end'] = len(ticks)
            notes.append(cur)
    mark_echoes(notes)
    for n in notes:
        loud = np.array(n['loud'])
        loud = loud * note_weight(n, prio, mod)
        # value from each tick on: loudness summed over the window ahead
        c = np.concatenate([np.cumsum(loud[::-1])[::-1], [0.0]])
        idx = np.arange(len(loud))
        n['val'] = c[idx] - c[np.minimum(idx + WINDOW, len(loud))]
        n['loud'] = loud
    return notes


# slot = (stream voice id, side 0 left / 1 right, mixed by the CPU)
SLOTS_4 = [(0, 0, False), (1, 1, False), (2, 1, False), (3, 0, False)]


def slots_for(mixmask):
    """Paula channels in mixmask are CPU mixers of 2 voices each (stream ids
    4,5 for the first mixer channel, 6,7 for the second)."""
    side = {0: 0, 1: 1, 2: 1, 3: 0}
    sl = [(c, side[c], False) for c in range(4) if not mixmask & (1 << c)]
    k = 4
    for c in range(4):
        if mixmask & (1 << c):
            sl += [(k, side[c], True), (k + 1, side[c], True)]
            k += 2
    return sl


def allocate(mod, ticks, table, chmap, prio=None, slots=SLOTS_4):
    """Per tick (marker, res[8], tempo); res[id] = (smp, pos, hz, vol, trig, virt)
    or None, id = stream voice id of the slot."""
    notes = [n for n in extract_notes(mod, ticks, chmap, prio) if n['smp'] in table]
    starts = {}
    for i, n in enumerate(notes):
        starts.setdefault(n['start'], []).append(i)
    ns = len(slots)
    chan = [None] * ns           # note index per slot
    active = set()
    out = []

    def own(ni, p):
        return slots[p][1] == notes[ni]['virt'] % 2

    def worth(ni, t, p):
        n = notes[ni]
        k = t - n['start']
        if k >= len(n['val']):
            return 0.0
        v = n['val'][k]
        if not own(ni, p):
            v *= CROSS_SIDE
        return v

    for t, (mk, voices, tempo) in enumerate(ticks):
        for p in range(ns):
            if chan[p] is not None and notes[chan[p]]['end'] <= t:
                chan[p] = None
        active = {ni for ni in active if notes[ni]['end'] > t}
        new = sorted(starts.get(t, []), key=lambda ni: -notes[ni]['val'][0])
        active |= set(new)
        triggered = set()
        waiting = sorted((ni for ni in active if ni not in chan and ni not in new),
                         key=lambda ni: -notes[ni]['val'][min(t - notes[ni]['start'], len(notes[ni]['val']) - 1)])
        for ni in new + waiting:
            n = notes[ni]
            k = t - n['start']
            onset = ni in new
            if not onset and n['loud'][k] < QUIET * 4:
                continue
            best = None
            # own side first, then direct (hardware) slots before mixed ones
            for p in sorted(range(ns), key=lambda p: (not own(ni, p), slots[p][2])):
                cur = chan[p]
                if cur is None:
                    cand = (1e9 - (1e6 if slots[p][2] else 0), p)
                else:
                    if cur in triggered:
                        continue
                    cn = notes[cur]
                    ck = t - cn['start']
                    if cn['loud'][ck] < QUIET:
                        cand = (1e8 - cn['val'][ck], p)
                    else:
                        mine = worth(ni, t, p) * (ONSET_BONUS if onset else 1.0)
                        theirs = worth(cur, t, p) * STAY_BONUS
                        if mine <= theirs:
                            continue
                        cand = (mine - theirs, p)
                if not own(ni, p):
                    cand = (cand[0] * CROSS_SIDE, p)
                if best is None or cand[0] > best[0]:
                    best = cand
            if best is not None:
                chan[best[1]] = ni
                triggered.add(ni)
        res = [None] * 8
        for p in range(ns):
            ni = chan[p]
            if ni is None:
                continue
            n = notes[ni]
            st = voices[chmap[n['virt']]]
            if st is None:
                chan[p] = None
                continue
            smp, pos, hz, vol, trig = st
            if ni in triggered and trig is None:
                trig = int(pos)
            if ni not in triggered:
                trig = None
            res[slots[p][0]] = (smp, pos, hz, vol, trig, n['virt'])
        out.append((mk, res, tempo))
    return out
