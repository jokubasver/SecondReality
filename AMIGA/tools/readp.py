"""Reader for Future Crew's 'readp' picture format (.UP/.UH files):
16 byte header (magic, wid, hig, cols, add), palette (6 bit) at offset 16,
rows from add*16: u16 length + RLE bytes (bit 7 set = run of next byte)."""
import struct


def read(path):
    d = open(path, 'rb').read()
    magic, wid, hig, cols, add = struct.unpack_from('<HHHHH', d, 0)
    pal = [tuple(d[16 + i * 3:19 + i * 3]) for i in range(cols)]
    p = add * 16
    rows = []
    for y in range(hig):
        n = struct.unpack_from('<H', d, p)[0]
        p += 2
        end = p + n
        row = bytearray()
        while p < end:
            b = d[p]; p += 1
            if b & 0x80:
                row += bytes([d[p]]) * (b & 0x7f)
                p += 1
            else:
                row.append(b)
        rows.append(row[:wid] + bytes(max(0, wid - len(row))))
    return wid, hig, rows, pal
