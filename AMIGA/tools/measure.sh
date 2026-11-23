#!/bin/bash
# usage: tools/measure.sh part music order t1 t2 ...
# builds a test of one part with the live frame counter (FPSSTAT overlay:
# 4th number = slow waits << 16 | frames, over the last 100 waits) and
# writes the overlay lines at the given times to /tmp/claude-0/meas_<part>.png
P=$1; M=$2; O=$3; shift 3
cd "$(dirname "$0")/.."
OVERLAY=1 FPSSTAT=1 python3 build.py --test $P --music $M --order $O 2>&1 | grep -iE "error|Traceback"
EMU_ARGS="--floppy_drive_1=build/SecondReality2.adf" tools/emu.sh build/SecondReality1.adf /tmp/claude-0/meas_$P "$@"
python3 - "$P" "$@" <<'PY'
import sys
from PIL import Image, ImageDraw
p=sys.argv[1]; ts=sys.argv[2:]
o=Image.new('RGB',(1024,50*len(ts)))
for i,t in enumerate(ts):
    im=Image.open('/tmp/claude-0/meas_%s_%s.png'%(p,t)).convert('RGB').crop((0,30,1024,80))
    ImageDraw.Draw(im).text((2,2),t,fill=(255,255,0))
    o.paste(im,(0,i*50))
o.save('/tmp/claude-0/meas_%s.png'%p)
PY
