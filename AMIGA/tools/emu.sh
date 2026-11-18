#!/bin/bash
# usage: tools/emu.sh disk.adf outprefix t1 [t2 ...]   (screenshots at t seconds)
ADF=$1; OUT=$2; shift 2
export SDL_AUDIODRIVER=dummy
D=:$((90 + RANDOM % 9))
Xvfb $D -screen 0 1024x768x24 >/dev/null 2>&1 & XP=$!
sleep 1
DISPLAY=$D fs-uae --amiga_model=A500 --chip_memory=1024 --slow_memory=0 --fast_memory=0 \
  --floppy_drive_0="$ADF" --fullscreen=0 --window_width=960 --window_height=720 \
  --automatic_input_grab=0 --audio_driver=null --floppy_drive_speed=${FLOPPY_SPEED:-0} --accuracy=1 \
  --base_dir=/tmp/claude-0/fsuae ${EMU_ARGS} >/tmp/claude-0/fsuae.log 2>&1 & FP=$!
T0=0
for t in "$@"; do
  sleep $((t - T0)); T0=$t
  DISPLAY=$D import -window root "${OUT}_$t.png" || echo "screenshot failed at $t"
done
kill $FP 2>/dev/null; sleep 0.5; kill $XP 2>/dev/null
