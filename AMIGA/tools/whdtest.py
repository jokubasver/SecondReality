#!/usr/bin/env python3
"""Builds build/whdtest.adf: a boot disk that stands in for WHDLoad (see
src/whdload/harness.S).  Run it in DF0 with the demo ADFs in DF1 and DF2 and
fast memory, e.g.:
  EMU_ARGS="--floppy_drive_1=build/SecondReality1.adf
            --floppy_drive_2=build/SecondReality2.adf "
  FAST_MEM=8192
  tools/emu.sh build/whdtest.adf shot 60
"""
import os, sys, subprocess
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, ROOT)
import mkadf
from build import asm_bin, B

# WHD_CUSTOM1=n in the environment: the CUSTOM1 tooltype the fake WHDLoad reports
h = asm_bin(os.path.join(ROOT, 'src/whdload/harness.S'), os.path.join(B, 'harness.bin'),
            [('WHD_CUSTOM1', int(os.environ.get('WHD_CUSTOM1', '0')))])
h += open(os.path.join(B, 'slave.bin'), 'rb').read()
size = (len(h) + 511) & ~511
src = open(os.path.join(ROOT, 'src/kernel/boot.S')).read().replace('moveq\t#2,d1', 'moveq\t#4,d1')
assert 'moveq\t#4,d1' in src
p = os.path.join(B, 'hboot.S')
open(p, 'w').write(src)
bb = asm_bin(p, os.path.join(B, 'hboot.bin'), [('KSIZE', size)])
disk = bytearray(901120)
disk[0:1024] = mkadf.make_bootblock(bb + bytes(1024 - 12 - len(bb)))
disk[1024:1024 + len(h)] = h
open(os.path.join(B, 'whdtest.adf'), 'wb').write(disk)
print('build/whdtest.adf')
