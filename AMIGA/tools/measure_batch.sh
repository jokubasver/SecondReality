#!/bin/bash
# usage: tools/measure_batch.sh "part:music:order" ...   (3 emulators at a time)
# overlay sheets in /tmp/claude-0/mb/<part>.png (4th number: slow waits << 16 | frames per 100 waits)
cd "$(dirname "$0")/.."
mkdir -p /tmp/claude-0/mb
for spec in "$@"; do
  IFS=: read P M O <<< "$spec"
  env OVERLAY=1 ${PROF:-FPSSTAT}=1 python3 build.py --test $P --music $M --order $O 2>&1 | grep -iE "error|Traceback"
  mkdir -p /tmp/claude-0/mb/$P && cp build/SecondReality1.adf build/SecondReality2.adf /tmp/claude-0/mb/$P/
done
TS=${TS:-$(seq 25 5 110)}
run() {
  P=$1
  EMU_ARGS="--floppy_drive_1=/tmp/claude-0/mb/$P/SecondReality2.adf" tools/emu.sh /tmp/claude-0/mb/$P/SecondReality1.adf /tmp/claude-0/mb/$P/s $TS >/dev/null 2>&1
  python3 - "$P" $TS <<'PY'
import sys
from PIL import Image, ImageDraw
p=sys.argv[1]; ts=sys.argv[2:]
o=Image.new('RGB',(1024,40*len(ts)))
for i,t in enumerate(ts):
    try: im=Image.open('/tmp/claude-0/mb/%s/s_%s.png'%(p,t)).convert('RGB').crop((0,35,1024,75))
    except Exception: continue
    ImageDraw.Draw(im).text((2,2),t,fill=(255,255,0))
    o.paste(im,(0,i*40))
o.save('/tmp/claude-0/mb/%s.png'%p)
PY
}
i=0
for spec in "$@"; do
  IFS=: read P M O <<< "$spec"
  run $P &
  i=$((i+1))
  if [ $((i % 3)) = 0 ]; then wait; fi
done
wait
