#!/bin/bash
# usage: tools/emuburst.sh disk.adf outprefix start_seconds count
ADF=$1; OUT=$2; START=$3; N=$4
export SDL_AUDIODRIVER=dummy
D=:$((90 + RANDOM % 9))
Xvfb $D -screen 0 1024x768x24 >/dev/null 2>&1 & XP=$!
sleep 1
DISPLAY=$D fs-uae --amiga_model=A500 --chip_memory=1024 --slow_memory=0 --fast_memory=0 \
  --floppy_drive_0="$ADF" --fullscreen=0 --window_width=960 --window_height=720 \
  --automatic_input_grab=0 --audio_driver=null --floppy_drive_speed=${FLOPPY_SPEED:-0} --accuracy=1 \
  --base_dir=/tmp/claude-0/fsuae ${EMU_ARGS} >/tmp/claude-0/fsuae.log 2>&1 & FP=$!
sleep $START
for i in $(seq -w 1 $N); do
  DISPLAY=$D import -window root "${OUT}_$i.png" 2>/dev/null
done
kill $FP 2>/dev/null; sleep 0.5; kill $XP 2>/dev/null
