#!/usr/bin/env python3
"""Build a raw (non-DOS) bootable ADF: bootblock + data at absolute byte offsets."""
import struct, sys

ADF_SIZE = 901120

def bootblock_checksum(bb):
    s = 0
    for i in range(0, 1024, 4):
        if i == 4: continue
        s += struct.unpack('>I', bb[i:i+4])[0]
        if s > 0xffffffff: s = (s + 1) & 0xffffffff
    return (~s) & 0xffffffff

def make_bootblock(code):
    assert len(code) <= 1024 - 12, "bootblock code too large: %d" % len(code)
    bb = bytearray(1024)
    bb[0:4] = b'DOS\0'
    struct.pack_into('>I', bb, 8, 880)
    bb[12:12+len(code)] = code
    struct.pack_into('>I', bb, 4, bootblock_checksum(bb))
    return bb

if __name__ == '__main__':
    out, boot = sys.argv[1], sys.argv[2]
    img = bytearray(ADF_SIZE)
    img[0:1024] = make_bootblock(open(boot, 'rb').read())
    for spec in sys.argv[3:]:
        off, fn = spec.split('@')[1], spec.split('@')[0]
        d = open(fn, 'rb').read(); off = int(off, 0)
        assert off + len(d) <= ADF_SIZE
        img[off:off+len(d)] = d
    open(out, 'wb').write(img)
