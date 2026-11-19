"""Extract data segments from Intel OMF object files (LEDATA/LIDATA records)."""
import struct


def segments(path):
    d = open(path, 'rb').read()
    p = 0
    lnames = ['']
    segdefs = []
    data = {}
    pubs = {}
    while p < len(d):
        t = d[p]
        ln = struct.unpack_from('<H', d, p + 1)[0]
        body = d[p + 3:p + 3 + ln - 1]
        if t == 0x96:
            q = 0
            while q < len(body):
                l = body[q]
                lnames.append(body[q + 1:q + 1 + l].decode('latin1'))
                q += 1 + l
        elif t in (0x98, 0x99):
            if t == 0x98:
                size = struct.unpack_from('<H', body, 1)[0]
                q = 3
            else:
                size = struct.unpack_from('<I', body, 1)[0]
                q = 5
            if body[0] & 2 and t == 0x98 and size == 0:
                size = 0x10000
            segdefs.append((lnames[body[q]] if body[q] < len(lnames) else '?', size))
        elif t in (0xA0, 0xA1):
            seg = body[0]
            if t == 0xA0:
                off = struct.unpack_from('<H', body, 1)[0]
                dat = body[3:]
            else:
                off = struct.unpack_from('<I', body, 1)[0]
                dat = body[5:]
            buf = data.setdefault(seg, bytearray())
            if len(buf) < off + len(dat):
                buf += bytes(off + len(dat) - len(buf))
            buf[off:off + len(dat)] = dat
        elif t in (0x90, 0x91):
            q = 2 if not (body[0] == 0 and body[1] == 0) else 4
            while q < len(body):
                l = body[q]
                n = body[q + 1:q + 1 + l].decode('latin1')
                q += 1 + l
                off = struct.unpack_from('<H', body, q)[0]
                q += 3
                pubs[n] = off
        p += 3 + ln
    return segdefs, data, pubs
