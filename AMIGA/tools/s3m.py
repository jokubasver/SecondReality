"""Minimal S3M parser."""
import struct

class Sample:
    pass

class S3M:
    def __init__(self, path):
        d = open(path, 'rb').read()
        self.data = d
        self.title = d[:28].split(b'\0')[0].decode('latin1')
        ordnum, insnum, patnum, flags, cwtv, ffi = struct.unpack_from('<HHHHHH', d, 0x20)
        self.gvol, self.speed, self.tempo, self.mvol = d[0x30], d[0x31], d[0x32], d[0x33]
        self.chset = list(d[0x40:0x60])
        p = 0x60
        self.orders = list(d[p:p+ordnum]); p += ordnum
        insp = struct.unpack_from('<%dH' % insnum, d, p); p += 2*insnum
        patp = struct.unpack_from('<%dH' % patnum, d, p); p += 2*patnum
        self.ffi = ffi
        self.samples = []
        for ip in insp:
            o = ip * 16
            s = Sample()
            s.type = d[o]
            s.name = d[o+0x30:o+0x30+28].split(b'\0')[0].decode('latin1')
            memseg = (d[o+0x0d] << 16) | struct.unpack_from('<H', d, o+0x0e)[0]
            s.length, s.loopbeg, s.loopend = struct.unpack_from('<III', d, o+0x10)
            s.vol = d[o+0x1c]
            s.flags = d[o+0x1f]
            s.c2spd = struct.unpack_from('<I', d, o+0x20)[0]
            s.loop = bool(s.flags & 1) and s.loopend > s.loopbeg
            if s.type == 1 and s.length:
                raw = d[memseg*16: memseg*16 + s.length]
                if ffi == 2:  # unsigned
                    raw = bytes((b - 128) & 0xff for b in raw)
                s.pcm = raw  # signed 8-bit
            else:
                s.pcm = b''
            if s.flags & 4: print('warning: 16-bit sample', s.name)
            self.samples.append(s)
        self.patterns = []
        for pp in patp:
            rows = [[None]*32 for _ in range(64)]
            if pp:
                o = pp * 16
                ln = struct.unpack_from('<H', d, o)[0]
                # Future Crew's STMIK scrambles pattern bytes: b ^= ((offset*4)^offset)&255
                pd = bytearray(d[o:o+ln])
                for k in range(2, ln): pd[k] ^= ((k * 4) ^ k) & 0xff
                d2 = d; d = pd; q = 2; r = 0
                while r < 64:
                    b = d[q]; q += 1
                    if b == 0:
                        r += 1; continue
                    ch = b & 31
                    note = ins = vol = cmd = info = None
                    if b & 32:
                        note, ins = d[q], d[q+1]; q += 2
                    if b & 64:
                        vol = d[q]; q += 1
                    if b & 128:
                        cmd, info = d[q], d[q+1]; q += 2
                    rows[r][ch] = (note, ins, vol, cmd, info)
                d = d2
            self.patterns.append(rows)
