#!/bin/bash
# usage: tools/bench3d.sh name   - VISU engine benchmark (V3PROF build, fixed
# animation steps): totals of the first 150 draws of U2E and U2A, sheet in
# /tmp/claude-0/prof/<name>.png
cd "$(dirname "$0")/.."
mkdir -p /tmp/claude-0/prof
TS="$(seq 118 1 127)" PROF="V3PROF=${V3PROF:-1} env" tools/measure_batch.sh u2e:0:18 u2a:0:10 >/dev/null 2>&1
python3 - "$1" <<'PY'
import sys
from PIL import Image
o=Image.new('RGB',(1024,800))
for i,p in enumerate(('u2e','u2a')):
    o.paste(Image.open('/tmp/claude-0/mb/%s.png'%p).crop((0,0,1024,400)),(0,i*400))
o.save('/tmp/claude-0/prof/%s.png'%sys.argv[1])
PY
