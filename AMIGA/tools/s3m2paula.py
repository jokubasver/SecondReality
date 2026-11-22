#!/usr/bin/env python3
"""
Convert an 8-channel Scream Tracker 3 module into an Amiga Paula register
stream plus a set of resampled 8-bit samples.

The S3M is emulated tick by tick (following the behaviour of Future Crew's
STMIK 3.00 player).  The 8 virtual voices are then mapped onto Paula's 4
hardware channels: the left voices onto channels 0/3 and the right voices onto
1/2, choosing on every tick the two most audible voices per side.

Outputs:
  <out>.smp  sample bank (loaded into chip RAM)
  <out>_<order>.str  one register stream per requested start order
"""
import sys, struct, math, argparse
import numpy as np
from s3m import S3M

PAULA_CLOCK = 3546895          # PAL
CIA_CLOCK = 709379             # PAL E-clock
MIN_PERIOD = 124
GAIN_MAX = 2.0
MIX_PERIOD = 428                # mixer channels: 8287 Hz
NT = [1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907]
SINE = [0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
        255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24]


def st3_period(note, c2spd):
    o, n = note >> 4, note & 15
    if n > 11 or c2spd == 0:
        return None
    return 8363 * 16 * (NT[n] >> o) // c2spd


def period_hz(p):
    return 14317456.0 / p if p > 0 else 0.0


class Voice:
    def __init__(self):
        self.smp = 0          # 1-based sample number, 0 = none
        self.vol = 0
        self.period = 0       # base period (ST3 units)
        self.outperiod = 0
        self.outvol = 0
        self.target = 0
        self.porta_mem = 0
        self.vib_mem = 0
        self.mem = 0          # shared memory for D,E,F,I,J,K,L,Q,R,S
        self.off_mem = 0
        self.vibpos = 0
        self.playing = False
        self.pos = 0.0        # position in original sample frames
        self.trig = None      # offset (frames) if retriggered this tick
        self.retrig_cnt = 0
        self.note = 255
        self.arp = 0


class Emu:
    """Plays an S3M and yields, per tick, the state of each virtual voice."""

    def __init__(self, mod, start_order, nchan=8):
        self.m = mod
        self.nchan = nchan
        self.v = [Voice() for _ in range(nchan)]
        self.speed = mod.speed
        self.tempo = mod.tempo
        self.gvol = mod.gvol
        self.ordi = start_order
        self.row = 0
        self.tick = 0
        self.plus = 0
        self.max_pos = {}
        self.freqs = {}
        self.ended = False
        self.visited = set()
        self._seek_order(start_order, first=True)

    def _seek_order(self, oi, first=False):
        orders = self.m.orders
        self.plus = 0
        while True:
            if oi >= len(orders) or orders[oi] == 255:
                self.ended = True
                return
            if orders[oi] == 254:
                self.plus |= 2
                oi += 1
                continue
            break
        if oi + 1 < len(orders) and orders[oi + 1] == 254:
            self.plus |= 1
        self.ordi = oi
        self.pat = orders[oi]

    def _sample(self, n):
        if n <= 0 or n > len(self.m.samples):
            return None
        s = self.m.samples[n - 1]
        return s if s.pcm else None

    def _trigger(self, v, offset=0):
        s = self._sample(v.smp)
        if s is None:
            v.playing = False
            return
        if offset >= len(s.pcm):
            if s.loop:
                offset = s.loopend - 1
            else:
                v.playing = False
                return
        v.playing = True
        v.pos = float(offset)
        v.trig = offset

    def _row_tick0(self, ch, cell):
        v = self.v[ch]
        note, ins, vol, cmd, info = cell
        cmd = cmd or 0
        info = info or 0
        if cmd and info:
            if cmd == 7:
                v.porta_mem = info
            elif cmd in (8, 21):
                v.vib_mem = info
            elif cmd == 15:
                v.off_mem = info
            else:
                v.mem = info
        if cmd and not info:
            if cmd == 7: info = v.porta_mem
            elif cmd in (8, 21): info = v.vib_mem
            elif cmd == 15: info = v.off_mem
            elif cmd in (4, 5, 6, 9, 10, 11, 12, 17, 18): info = v.mem
        self.cur[ch] = (cmd, info)
        if ins:
            s = self._sample(ins)
            v.smp = ins
            if s is not None:
                v.vol = s.vol
        if note is not None and note != 255:
            if note == 254:
                v.playing = False
                v.vol = 0
            else:
                s = self._sample(v.smp)
                if s is not None:
                    p = st3_period(note, s.c2spd)
                    if p:
                        v.note = note
                        if cmd in (7, 12) and v.playing:
                            v.target = p
                        else:
                            v.period = p
                            v.target = p
                            v.vibpos = 0
                            off = (info << 8) if cmd == 15 else 0
                            self._trigger(v, off)
                            v.retrig_cnt = 0
        if vol is not None:
            v.vol = min(vol, 64)
        # tick-0 effects
        if cmd == 1 and info:
            self.speed = info
        elif cmd == 2:
            self.jump = info
        elif cmd == 3:
            self.brk = (info >> 4) * 10 + (info & 15)
        elif cmd == 20 and info >= 0x20:
            self.tempo = info

    def _effects(self, ch):
        v = self.v[ch]
        cmd, info = self.cur[ch]
        t = self.tick
        v.outperiod = v.period
        v.arp = 0
        if cmd in (4, 11, 12):          # volume slide (every tick in ST3.00)
            x, y = info >> 4, info & 15
            if y == 0:
                if t or True: v.vol += x
            elif x == 0:
                v.vol -= y
            elif y == 15:
                if t == 0: v.vol += x
            elif x == 15:
                if t == 0: v.vol -= y
            v.vol = max(0, min(64, v.vol))
        if cmd in (5, 6):               # portamento down / up
            x, y = info >> 4, info & 15
            if x == 15:
                d = y * 4 if t == 0 else 0
            elif x == 14:
                d = y if t == 0 else 0
            else:
                d = info * 4 if t else 0
            v.period += d if cmd == 5 else -d
            v.period = max(56, min(32767, v.period))
            v.outperiod = v.period
        if cmd in (7, 12) and t:        # tone portamento
            sp = (v.porta_mem if cmd == 12 else info) * 4
            if v.period < v.target:
                v.period = min(v.target, v.period + sp)
            elif v.period > v.target:
                v.period = max(v.target, v.period - sp)
            v.outperiod = v.period
        if cmd in (8, 11, 21) and t:    # vibrato
            inf = v.vib_mem if cmd == 11 else info
            spd, dep = inf >> 4, inf & 15
            sv = SINE[v.vibpos & 31]
            if v.vibpos & 32: sv = -sv
            d = (sv * dep) >> (7 if cmd == 21 else 5)
            v.outperiod = v.period + d
            v.vibpos = (v.vibpos + spd) & 63
        if cmd == 10 and info:          # arpeggio
            k = t % 3
            if k == 1: v.arp = info >> 4
            elif k == 2: v.arp = info & 15
        if cmd == 17 and info & 15:     # retrig
            v.retrig_cnt += 1
            if v.retrig_cnt >= (info & 15):
                v.retrig_cnt = 0
                x = info >> 4
                vv = v.vol
                vv = {1: vv - 1, 2: vv - 2, 3: vv - 4, 4: vv - 8, 5: vv - 16, 6: vv * 2 // 3,
                      7: vv // 2, 9: vv + 1, 10: vv + 2, 11: vv + 4, 12: vv + 8, 13: vv + 16,
                      14: vv * 3 // 2, 15: vv * 2}.get(x, vv)
                v.vol = max(0, min(64, vv))
                self._trigger(v, 0)
        if cmd == 19 and (info >> 4) == 0xC and t == (info & 15):   # note cut
            v.vol = 0

    def run_tick(self):
        """Advance one tick. Returns row marker (order, row, plus) or None."""
        marker = None
        for v in self.v:
            v.trig = None
        if self.tick == 0:
            self.jump = None
            self.brk = None
            self.cur = [(0, 0)] * self.nchan
            self.delayed = {}
            rows = self.m.patterns[self.pat][self.row]
            for ch in range(self.nchan):
                cell = rows[ch]
                if cell is None:
                    continue
                cmd, info = cell[3] or 0, cell[4] or 0
                if cmd == 19 and (info >> 4) == 0xD and (info & 15):
                    self.delayed[ch] = ((info & 15), cell)
                    continue
                self._row_tick0(ch, cell)
            marker = (self.ordi, self.row, self.plus)
            self.visited.add((self.ordi, self.row))
        for ch, (dt, cell) in list(self.delayed.items()):
            if dt == self.tick:
                c = (cell[0], cell[1], cell[2], 0, 0)
                self._row_tick0(ch, c)
                del self.delayed[ch]
        for ch in range(self.nchan):
            self._effects(ch)
        # advance positions
        tickrate = self.tempo * 2 / 5.0
        out = []
        for i, v in enumerate(self.v):
            s = self._sample(v.smp)
            p = max(56, v.outperiod)
            hz = period_hz(p) * (2 ** (v.arp / 12.0)) if p else 0
            vol = v.vol * self.gvol // 64
            if v.playing and s is not None:
                if v.trig is None:
                    pass
                st = (v.smp, v.pos, hz, vol, v.trig)
                v.pos += hz / tickrate
                if v.pos >= len(s.pcm):
                    if s.loop:
                        ll = s.loopend - s.loopbeg
                        v.pos = s.loopbeg + (v.pos - s.loopbeg) % ll
                    else:
                        v.playing = False
                mp = self.max_pos.get(v.smp, 0)
                self.max_pos[v.smp] = max(mp, min(len(s.pcm), v.pos if v.playing else len(s.pcm)))
                if vol:
                    self.freqs.setdefault(v.smp, []).append(hz)
                out.append(st)
            else:
                out.append(None)
        # advance tick / row
        self.tick += 1
        if self.tick >= self.speed:
            self.tick = 0
            if self.jump is not None or self.brk is not None:
                no = self.jump if self.jump is not None else self.ordi + 1
                self.row = self.brk if self.brk is not None else 0
                if self.jump is not None and self.jump <= self.ordi:
                    self.ended = True   # backwards jump = song loop
                self._seek_order(no)
            else:
                self.row += 1
                if self.row >= 64:
                    self.row = 0
                    self._seek_order(self.ordi + 1)
        return marker, out


def simulate(mod, start_order, max_ticks=200000, stop_order=None):
    emu = Emu(mod, start_order)
    ticks = []
    while not emu.ended and len(ticks) < max_ticks:
        if stop_order is not None and emu.ordi >= stop_order and emu.tick == 0 and emu.row == 0:
            break
        mk, out = emu.run_tick()
        ticks.append((mk, out, emu.tempo))
    return emu, ticks


# ---------------------------------------------------------------- resampling
def sinc_resample(x, ratio, loopbeg=None, looplen=None, taps=16):
    """Resample signed float array x by ratio (<1 shrinks).  If looped, the
    signal is extended with loop repetitions so the filter sees continuity."""
    n = len(x)
    ext = taps * 2 + 8
    if looplen:
        tail = np.concatenate([x[loopbeg:loopbeg + looplen]] * (ext // looplen + 2))[:ext]
    else:
        tail = np.zeros(ext)
    xx = np.concatenate([np.zeros(ext), x, tail])
    outn = int(round(n * ratio))
    cutoff = min(1.0, ratio) * 0.95
    t = np.arange(outn) / ratio + ext
    i0 = np.floor(t).astype(np.int64)
    frac = t - i0
    acc = np.zeros(outn)
    for k in range(-taps + 1, taps + 1):
        d = k - frac
        w = cutoff * np.sinc(cutoff * d) * (0.5 + 0.5 * np.cos(np.pi * d / taps))
        acc += xx[i0 + k] * w
    return acc


def build_samples(mod, sims, budget, verbose=True):
    """Choose per-sample rate factors to fit 'budget' bytes and build the bank."""
    used = {}
    maxpos = {}
    vols = {}
    for emu, ticks in sims:
        for mk, voices, tempo in ticks:
            for st in voices:
                if st is not None and st[3] > 0:
                    vols.setdefault(st[0], []).append(st[3])
        for k, f in emu.freqs.items():
            used.setdefault(k, []).extend(f)
        for k, p in emu.max_pos.items():
            maxpos[k] = max(maxpos.get(k, 0), p)
    info = {}
    for k in sorted(used):
        s = mod.samples[k - 1]
        f = np.array(used[k])
        fmax = np.percentile(f, 99.5)
        f90 = np.percentile(f, 90)
        if s.loop:
            ln = s.loopend
        else:
            ln = int(min(len(s.pcm), math.ceil(maxpos.get(k, len(s.pcm))) + 16))
            # trim silent tail
            a = np.frombuffer(s.pcm[:ln], dtype=np.int8).astype(np.int32)
            nz = np.nonzero(np.abs(a) > 2)[0]
            if len(nz):
                ln = min(ln, int(nz[-1]) + 2)
        info[k] = dict(s=s, fmax=fmax, f90=f90, len=ln)

    def sizes(Q):
        tot = 0
        rs = {}
        for k, d in info.items():
            r = min(1.0, Q / d['f90'], (PAULA_CLOCK / (MIN_PERIOD + 2)) / d['fmax'])
            rs[k] = r
            tot += int(d['len'] * r) + 4
        return tot, rs

    lo, hi = 1000.0, 60000.0
    tot, rs = sizes(hi)
    if tot > budget:
        for _ in range(40):
            mid = (lo + hi) / 2
            if sizes(mid)[0] > budget: hi = mid
            else: lo = mid
        tot, rs = sizes(lo)
        if verbose: print('  bandwidth Q = %.0f Hz' % lo)
    bank = bytearray(4)     # 4 zero bytes at the start = silence for Paula
    table = {}
    # samples heard until late in the song first, so the bank can be cut
    # short once the others are no longer needed (see write_bank)
    lastuse = {}
    for emu, ticks in sims:
        o = 0
        for mk, voices, tempo in ticks:
            if mk: o = mk[0]
            for st in voices:
                if st is not None and st[3] > 0:
                    lastuse[st[0]] = max(lastuse.get(st[0], 0), o)
    for k, d in sorted(info.items(), key=lambda kd: (-lastuse.get(kd[0], 0), kd[0])):
        s = d['s']
        r = rs[k]
        x = np.frombuffer(s.pcm[:d['len']], dtype=np.int8).astype(np.float64)
        if s.loop:
            lb, ll = s.loopbeg, s.loopend - s.loopbeg
            newll = max(2, int(round(ll * r / 2)) * 2)
            r = newll / ll
            newlb = int(round(lb * r / 2)) * 2
            y = sinc_resample(x, r, lb, ll)
            y = y[:newlb + newll]
            if len(y) < newlb + newll:
                y = np.concatenate([y, np.zeros(newlb + newll - len(y))])
        else:
            y = sinc_resample(x, r)
            if len(y) & 1: y = np.concatenate([y, [0]])
            newlb = newll = 0
        # normalise towards full scale (less quantisation noise), Paula
        # volume compensates; limited so that typical volumes stay >= 16
        peak = max(1.0, float(np.max(np.abs(y))) if len(y) else 1.0)
        medvol = float(np.median(vols.get(k, [64])))
        g = max(1.0, min(127.0 / peak, GAIN_MAX, medvol / 16.0))
        y = np.clip(np.round(y * g), -128, 127).astype(np.int8)
        if len(y) < 2:
            y = np.zeros(2, dtype=np.int8)
        off = len(bank)
        bank += y.tobytes()
        if len(bank) & 1: bank += b'\0'
        table[k] = dict(off=off, len=len(y), lb=newlb, ll=newll, r=r, g=g, last=lastuse.get(k, 0))
        if verbose:
            print('  smp %2d %-28s %6d -> %6d  r=%.3f g=%.2f fmax=%.0f%s' % (
                k, s.name[:28], len(s.pcm), len(y), r, g, d['fmax'], ' loop' if s.loop else ''))
    return bank, table


# ---------------------------------------------------------------- allocation
SIDES = [([0, 2, 4, 6], [0, 3]), ([1, 3, 5, 7], [1, 2])]


def rms_at(mod, smp, pos, cache={}):
    key = (id(mod), smp)
    if key not in cache:
        a = np.frombuffer(mod.samples[smp - 1].pcm, dtype=np.int8).astype(np.float64)
        # windowed RMS envelope, 256 frame resolution
        n = (len(a) + 255) // 256
        env = np.array([np.sqrt(np.mean(a[i * 256:(i + 1) * 256] ** 2)) for i in range(n)])
        cache[key] = env
    env = cache[key]
    i = int(pos) // 256
    return env[min(i, len(env) - 1)] if len(env) else 0.0


def allocate(mod, ticks, table, chmap):
    """Returns per tick a list of 4 Paula channel states:
       (sampleno, pos_frames_in_original, hz, vol, triggered_offset or None, virt)"""
    assign = [None] * 4
    out = []
    for mk, voices, tempo in ticks:
        res = [None] * 4
        for virt_list, pch in SIDES:
            cands = []
            for vi in virt_list:
                st = voices[chmap[vi]] if chmap[vi] is not None else None
                if st is None or st[3] == 0 or st[0] not in table:
                    continue
                smp, pos, hz, vol, trig = st
                score = vol * (rms_at(mod, smp, pos) + 4)
                if trig is not None:
                    score *= 2.5
                if vi in assign:
                    score *= 1.6
                cands.append((score, vi))
            cands.sort(reverse=True)
            chosen = [vi for _, vi in cands[:2]]
            newp = set()
            for p in pch:
                if assign[p] not in chosen:
                    assign[p] = None
            for vi in chosen:
                if vi not in [assign[p] for p in pch]:
                    p = [q for q in pch if assign[q] is None][0]
                    assign[p] = vi
                    newp.add(p)
            for p in pch:
                a = assign[p]
                if a is None:
                    continue
                smp, pos, hz, vol, trig = voices[chmap[a]]
                if p in newp and trig is None:
                    trig = int(pos)
                res[p] = (smp, pos, hz, vol, trig, a)
        out.append((mk, res, tempo))
    return out


# ---------------------------------------------------------------- stream
def encode_stream(alloc, table, mixmask=0):
    """Returns a list of tick records (bytes).  Record format:
         u8 ctl: 0x80 end of stream, 0x81 end of chunk, 0x40|n: n (1..63)
                 ticks without any change; otherwise bit0 voice mask follows,
                 bit4 row marker, bit5 tempo change
         [bit5] u16 CIA timer value
         [bit4] u8 order, u8 row, u8 plus
         [bit0] u8 voice mask: bits 0-3 Paula channels played directly,
                bits 4-7 voices mixed by the CPU (2 per mixer channel)
         per voice in the mask: u8 cflags:
             bit0 trigger: u8 sample index, u16 offset in words
             bit1 period delta: s8      bit2 period absolute: u16
             bit3 volume: u8            bit4 stop (silence the voice)
       Volumes: 0..64 Paula volume without mixer channels; with them, both
       direct and mixed voices use 0..32 (a mixer channel adds 2 voices).
    """
    recs = []
    idx = {k: i for i, k in enumerate(sorted(table))}
    state = [dict(per=None, vol=None, on=False) for _ in range(8)]
    vdiv = 2.0 if mixmask else 1.0
    if mixmask:
        # the mixer plays its voices 1-2 blocks (15-31 ms) late: send their
        # events one tick early
        alloc = [(mk, list(res[:4]) + list((alloc[i + 1][1] if i + 1 < len(alloc) else [None] * 8)[4:]), tempo)
                 for i, (mk, res, tempo) in enumerate(alloc)]
    cur_tempo = None
    for mk, res, tempo in alloc:
        ctl = 0
        body = bytearray()
        if tempo != cur_tempo:
            ctl |= 0x20
            cur_tempo = tempo
            body += struct.pack('>H', int(round(CIA_CLOCK / (tempo * 2 / 5.0))))
        if mk is not None:
            ctl |= 0x10
            body += bytes([mk[0], mk[1], mk[2]])
        mask = 0
        vb = bytearray()
        for p in range(len(res)):
            st = res[p]
            cf = 0
            cb = bytearray()
            s = state[p]
            if st is None:
                if s['on']:
                    cf |= 16
                    s['on'] = False
                    s['vol'] = None
            else:
                smp, pos, hz, vol, trig, virt = st
                t = table[smp]
                if trig is not None:
                    offw = int(trig * t['r']) // 2
                    if offw * 2 >= t['len'] - 2:
                        offw = t['lb'] // 2 if t['ll'] else None
                    if offw is not None:
                        cf |= 1
                        cb += struct.pack('>BH', idx[smp], offw)
                        s['on'] = True
                per = int(round(PAULA_CLOCK / (hz * t['r']))) if hz > 0 else 65535
                per = max(MIN_PERIOD, min(65535, per))
                if per != s['per']:
                    if s['per'] is not None and -128 <= per - s['per'] <= 127:
                        cf |= 2
                        cb += struct.pack('>b', per - s['per'])
                    else:
                        cf |= 4
                        cb += struct.pack('>H', per)
                    s['per'] = per
                vol = min(64, int(round(vol / t.get('g', 1.0) / vdiv)))
                if vol != s['vol']:
                    cf |= 8
                    cb += bytes([vol])
                    s['vol'] = vol
            if cf:
                mask |= 1 << p
                vb += bytes([cf]) + cb
        if mask:
            ctl |= 1
            body += bytes([mask]) + vb
        recs.append(bytes([ctl]) + body)
    out = []
    i = 0
    while i < len(recs):
        if recs[i] == b'\0':
            n = 1
            while i + n < len(recs) and recs[i + n] == b'\0' and n < 63:
                n += 1
            out.append(bytes([0x40 | n]))
            i += n
        else:
            out.append(recs[i])
            i += 1
    out.append(bytes([0x80]))
    return out


def write_chunked(path, recs, chunk=1024, mixmask=0, mixper=MIX_PERIOD):
    """Container: 'SRSM', u16 nchunks, u16 maxchunk, u16 mixer channel mask,
    u16 mixer Paula period, u32 offsets[n+1] (from the file start), packed chunks.
    Every chunk ends with 0x81 (continue in next chunk) except the last (0x80)."""
    import subprocess, tempfile, os
    chunks = []
    cur = bytearray()
    for r in recs:
        if r == bytes([0x80]):
            break
        if len(cur) + len(r) + 1 > chunk:
            chunks.append(bytes(cur) + b'\x81')
            cur = bytearray()
        cur += r
    chunks.append(bytes(cur) + b'\x80')
    packed = []
    for c in chunks:
        with tempfile.TemporaryDirectory() as td:
            fi, fo = os.path.join(td, 'i'), os.path.join(td, 'o')
            open(fi, 'wb').write(c)
            subprocess.run([os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lzpack'), fi, fo],
                           check=True, capture_output=True)
            p = open(fo, 'rb').read()
            if len(p) & 1: p += b'\0'
            packed.append(p)
    hdr = 4 + 8 + 4 * (len(chunks) + 1)
    offs = []
    o = hdr
    for p in packed:
        offs.append(o); o += len(p)
    offs.append(o)
    data = b'SRSM' + struct.pack('>HHHH', len(chunks), max(map(len, chunks)), mixmask, mixper) + \
        b''.join(struct.pack('>I', x) for x in offs) + b''.join(packed)
    open(path, 'wb').write(data)
    return len(data)


def write_bank(path, bank, table):
    """Bank file: u16 count, then per sample: u32 offset, u16 len/2, u16 loopstart/2,
       u16 looplen/2 (0 = one-shot); then PCM data (offsets are from the start of the
       PCM area, which begins right after the table)."""
    ks = sorted(table)
    hdr = struct.pack('>H', len(ks))
    for k in ks:
        t = table[k]
        hdr += struct.pack('>IHHH', t['off'], t['len'] // 2, t['lb'] // 2, t['ll'] // 2)
    if len(hdr) & 3: hdr += b'\0' * (4 - (len(hdr) & 3))
    open(path, 'wb').write(hdr + bank)
    return len(hdr) + len(bank)


def trim_size(table, order):
    """bank file bytes still needed once the song is at 'order' or later"""
    hdr = 2 + 10 * len(table)
    hdr += -hdr & 3
    end = 4
    for k, t in table.items():
        if t['last'] >= order:
            end = max(end, t['off'] + t['len'])
    return hdr + ((end + 1) & ~1)


def render_stream(path, alloc, table, bank, mixmask=0, rate=44100):
    """Renders what the Amiga plays for an allocation: direct voices as Paula
    plays them, mixed voices exactly as the kernel mixer computes them
    (8 bit, nearest sample, (s * q) >> 6 volume, 2 voices summed per
    mixer channel at the mixer rate), to a stereo WAV."""
    import wave
    smps = {k: np.frombuffer(bytes(bank[t['off']:t['off'] + t['len']]), dtype=np.int8).astype(np.int64)
            for k, t in table.items()}
    vdiv = 2.0 if mixmask else 1.0
    mixch = [c for c in range(4) if mixmask & (1 << c)]
    side = {0: 0, 1: 1, 2: 1, 3: 0}
    st = {}                                  # voice id -> dict(smp, pos16, per, vol)
    out = [[], []]
    macc = 0                                 # mixer samples, fraction (x 428)
    hacc = 0.0
    for mk, res, tempo in alloc:
        cia = int(round(CIA_CLOCK / (tempo * 2 / 5.0)))
        tick_s = cia / CIA_CLOCK
        n = int(round(rate * tick_s + hacc)); hacc += rate * tick_s - n
        macc += 5 * cia
        nm = macc // MIX_PERIOD; macc -= nm * MIX_PERIOD
        buf = [np.zeros(n), np.zeros(n)]
        for vid in range(len(res)):
            r = res[vid]
            if r is None:
                st.pop(vid, None)
                continue
            smp, pos, hz, vol, trig, virt = r
            t = table[smp]
            per = max(MIN_PERIOD, min(65535, int(round(PAULA_CLOCK / (hz * t['r']))))) if hz > 0 else 65535
            v = min(64, int(round(vol / t.get('g', 1.0) / vdiv)))
            s0 = st.get(vid)
            if trig is not None:
                offw = int(trig * t['r']) // 2
                if offw * 2 >= t['len'] - 2:
                    offw = t['lb'] // 2 if t['ll'] else None
                if offw is not None:
                    s0 = st[vid] = dict(smp=smp, pos=offw * 2 << 16)
            if s0 is None or s0['smp'] != smp:
                continue
            s0['per'] = per
            s0['vol'] = v
        # direct voices
        for vid, s0 in list(st.items()):
            if vid >= 4:
                continue
            t = table[s0['smp']]; a = smps[s0['smp']]
            step = PAULA_CLOCK / s0['per'] / rate
            idx = s0['pos'] / 65536.0 + step * np.arange(n)
            if t['ll']:
                lb, ll = t['lb'], t['ll']
                idx = np.where(idx >= lb + ll, lb + (idx - lb) % ll, idx)
                ok = np.ones(n, bool)
            else:
                ok = idx < len(a)
            ii = np.clip(idx.astype(np.int64), 0, len(a) - 1)
            buf[side[vid]] += np.where(ok, a[ii], 0) * s0['vol'] / 64.0
            np_ = s0['pos'] / 65536.0 + step * n
            if t['ll'] and np_ >= t['lb'] + t['ll']:
                np_ = t['lb'] + (np_ - t['lb']) % t['ll']
            s0['pos'] = int(np_ * 65536)
            if not t['ll'] and np_ >= len(a):
                del st[vid]
        # mixed voices
        for k, c in enumerate(mixch):
            mix = np.zeros(nm, np.int64)
            for vid in (4 + 2 * k, 5 + 2 * k):
                s0 = st.get(vid)
                if s0 is None:
                    continue
                t = table[s0['smp']]; a = smps[s0['smp']]
                step = (MIX_PERIOD << 16) // s0['per']
                pp = s0['pos'] + step * np.arange(nm, dtype=np.int64)
                ip = pp >> 16
                if t['ll']:
                    lb, ll = t['lb'], t['ll']
                    over = ip >= lb + ll
                    ip = np.where(over, lb + (ip - lb) % ll, ip)
                    ok = np.ones(nm, bool)
                else:
                    ok = ip < len(a)
                ip = np.clip(ip, 0, len(a) - 1)
                q = min(32, s0['vol'])
                mix += np.where(ok, (a[ip] * q) >> 6, 0)
                endp = s0['pos'] + step * nm
                if t['ll'] and (endp >> 16) >= t['lb'] + t['ll']:
                    e = (endp >> 16) - t['lb']
                    endp = ((t['lb'] + e % t['ll']) << 16) | (endp & 0xffff)
                s0['pos'] = endp
                if not t['ll'] and (endp >> 16) >= len(a):
                    del st[vid]
            mix = ((mix + 128) & 255) - 128     # 8 bit wrap like the hardware
            # hold each mixer sample until the next (Paula)
            if nm:
                ti = np.minimum((np.arange(n) * nm) // max(n, 1), nm - 1)
                buf[side[c]] += mix[ti]
        out[0].append(buf[0]); out[1].append(buf[1])
    L = np.concatenate(out[0]); R = np.concatenate(out[1])
    stv = np.stack([L, R], 1) / 256.0 * 0.84
    pcm = np.clip(stv * 32767, -32768, 32767).astype('<i2')
    w = wave.open(path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(pcm.tobytes()); w.close()


def render_wav(path, alloc, table, bank, rate=44100, seconds=None):
    """Debug: render what Paula would play to a stereo WAV."""
    import wave
    smps = {k: np.frombuffer(bytes(bank[t['off']:t['off'] + t['len']]), dtype=np.int8).astype(np.float32)
            for k, t in table.items()}
    chpos = [0.0] * 4
    chsmp = [None] * 4
    outL, outR = [], []
    for mk, res, tempo in alloc:
        n = int(round(rate / (tempo * 2 / 5.0)))
        bufL = np.zeros(n, np.float32); bufR = np.zeros(n, np.float32)
        for p in range(4):
            st = res[p]
            if st is None:
                chsmp[p] = None
                continue
            smp, pos, hz, vol, trig, virt = st
            t = table[smp]
            if trig is not None or chsmp[p] != smp:
                chpos[p] = int(trig * t['r']) & ~1 if trig is not None else pos * t['r']
                chsmp[p] = smp
            a = smps[smp]
            step = hz * t['r'] / rate
            idxs = chpos[p] + step * np.arange(n)
            if t['ll']:
                lb, ll = t['lb'], t['ll']
                idxs = np.where(idxs >= lb + ll, lb + (idxs - lb) % ll, idxs)
                valid = np.ones(n, bool)
            else:
                valid = idxs < len(a)
            ii = np.clip(idxs.astype(np.int64), 0, len(a) - 1)
            sig = np.where(valid, a[ii], 0) * vol / 64.0 / t.get('g', 1.0)
            if p in (0, 3): bufL += sig
            else: bufR += sig
            chpos[p] = chpos[p] + step * n
            if t['ll'] and chpos[p] >= t['lb'] + t['ll']:
                chpos[p] = t['lb'] + (chpos[p] - t['lb']) % t['ll']
        outL.append(bufL); outR.append(bufR)
        if seconds and sum(len(b) for b in outL) > seconds * rate:
            break
    L = np.concatenate(outL); R = np.concatenate(outR)
    st = np.stack([L, R], 1) / 256.0
    pcm = np.clip(st * 32767, -32768, 32767).astype('<i2')
    w = wave.open(path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
    w.writeframes(pcm.tobytes()); w.close()


def alias_inverted(mod, verbose=True):
    """The S3Ms carry phase-inverted copies of some samples for their Dolby
    Surround effect.  Paula has no surround: play the original instead (one
    copy in memory, and the allocator sees them as the same sound)."""
    import numpy as np
    alias = {}
    smp = mod.samples
    for b in range(len(smp)):
        if not smp[b].pcm:
            continue
        y = np.frombuffer(smp[b].pcm, dtype=np.int8).astype(np.int32)
        for a in range(b):
            A, B = smp[a], smp[b]
            if a + 1 in alias or not A.pcm or len(A.pcm) != len(B.pcm) or A.c2spd != B.c2spd or \
                    A.loop != B.loop or (A.loop and (A.loopbeg, A.loopend) != (B.loopbeg, B.loopend)):
                continue
            x = np.frombuffer(A.pcm, dtype=np.int8).astype(np.int32)
            if np.all((x + y == 0) | (x + y == -1)):
                alias[b + 1] = a + 1
                break
    for pat in mod.patterns:
        for row in pat:
            for ch, c in enumerate(row):
                if c and c[1] in alias:
                    row[ch] = (c[0], alias[c[1]], c[2], c[3], c[4])
    if verbose and alias:
        print('  inverted copies played as originals:', ', '.join('%d->%d' % kv for kv in sorted(alias.items())))
    return alias


def channel_map(mod):
    """virtual voice index -> S3M channel; left voices even, right odd"""
    order = [i for i, c in enumerate(mod.chset) if c < 16]
    lefts = [ch for ch in order if mod.chset[ch] < 8]
    rights = [ch for ch in order if 8 <= mod.chset[ch] < 16]
    chmap = [None] * 8
    for i, ch in enumerate(lefts[:4]): chmap[i * 2] = ch
    for i, ch in enumerate(rights[:4]): chmap[i * 2 + 1] = ch
    return chmap


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('s3m')
    ap.add_argument('out')
    ap.add_argument('--orders', default='0')
    ap.add_argument('--budget', type=int, default=240000)
    ap.add_argument('--wav', action='store_true')
    ap.add_argument('--prio', help='note priorities from a reference arrangement (expert_prio.py)')
    ap.add_argument('--old-alloc', action='store_true')
    ap.add_argument('--mixmask', type=int, default=0, help='Paula channels used as CPU mixers (2 voices each)')
    ap.add_argument('--trim-order', type=int, help='write <out>.trim: bank size needed from this order on')
    a = ap.parse_args()
    mod = S3M(a.s3m)
    alias_inverted(mod)
    chmap = channel_map(mod)
    starts = []
    stops = []
    for x in a.orders.split(','):
        if '-' in x:
            b, e = x.split('-'); starts.append(int(b)); stops.append(int(e))
        else:
            starts.append(int(x)); stops.append(None)
    sims = []
    for so, sto in zip(starts, stops):
        emu, ticks = simulate(mod, so, stop_order=sto)
        secs = sum(1.0 / (t[2] * 2 / 5.0) for t in ticks)
        print('order %d: %d ticks, %.1f s' % (so, len(ticks), secs))
        sims.append((emu, ticks))
    bank, table = build_samples(mod, sims, a.budget)
    size = write_bank(a.out + '.smp', bank, table)
    print('sample bank %d bytes' % size)
    if a.trim_order is not None:
        ts = trim_size(table, a.trim_order)
        open(a.out + '.trim', 'w').write('%d %d\n' % (a.trim_order, ts))
        print('from order %d the bank needs %d bytes' % (a.trim_order, ts))
    for so, (emu, ticks) in zip(starts, sims):
        if a.old_alloc:
            al = allocate(mod, ticks, table, chmap)
        else:
            import paula_alloc
            al = paula_alloc.allocate(mod, ticks, table, chmap,
                                      paula_alloc.load_prio(a.prio) if a.prio else None,
                                      paula_alloc.slots_for(a.mixmask))
        recs = encode_stream(al, table, a.mixmask)
        st = write_chunked('%s_%d.str' % (a.out, so), recs, mixmask=a.mixmask)
        print('stream from order %d: %d bytes raw, %d chunked+packed' % (so, sum(map(len, recs)), st))
        if a.wav:
            render_stream('%s_%d.wav' % (a.out, so), al, table, bank, a.mixmask)


if __name__ == '__main__':
    main()
