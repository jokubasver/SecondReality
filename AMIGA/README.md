# Second Reality - Amiga port

A port of Future Crew's Second Reality (PC, 1993) to the Amiga 500:
Kickstart 1.3, OCS/ECS, 1 MB chip RAM, no fast RAM.  It boots from two
disks with its own trackloader and does not use the operating system.

## Building

Needs `m68k-linux-gnu-gcc`/`binutils`, Python 3 with Pillow and pyelftools,
and a C compiler for the packer (`tools/lzpack.c`).  The original sources
and data are read from the directories next to this one.

    python3 build.py                 # build/SecondReality1.adf, SecondReality2.adf
    python3 build.py --release       # no debug aids (crash screen etc.)

Test builds:

    python3 build.py --test PART --music N --order O   # run one part in a loop,
                                                       # music N from order O
    python3 build.py --skip-intro                      # start at PAM
    OVERLAY=1 python3 build.py ...                     # loader/phase overlay

`tools/emu.sh disk.adf prefix t1 t2 ...` runs FS-UAE (A500, 1 MB chip)
headless and takes screenshots; put disk 2 in DF1 with
`EMU_ARGS="--floppy_drive_1=build/SecondReality2.adf"`.

## Disks

Disk 1 holds the kernel, music 0 (intro) and music 1, and the parts up to
TECHNO; disk 2 holds the rest and music 0 again for the end.  With one
drive the demo asks for disk 2; with a second drive it reads DF1.

## WHDLoad

The build also writes `build/whdload/SecondReality` (`SecondReality.slave`,
`disk.1`, `disk.2`, `ReadMe`).  The slave (`src/whdload/slave.S`) loads both
disk images into ExpMem (1.8 MB), copies the kernel to 0x400 and enters it
at 0x408; the kernel then reads files from memory instead of the drive.
F10 quits; the demo returns to WHDLoad after the end scroller.

`tools/whdtest.py` builds `build/whdtest.adf`, a boot disk that stands in
for WHDLoad in the emulator (reads the two ADFs from DF1/DF2 into fast
memory and calls the slave):

    python3 tools/whdtest.py
    FAST_MEM=8192 EMU_ARGS="--floppy_drive_1=build/SecondReality1.adf \
      --floppy_drive_2=build/SecondReality2.adf" tools/emu.sh build/whdtest.adf w 100

Release files are in `dist/`.

## Structure

- `src/kernel`: boot block, kernel (interrupts, trackloader, LZ unpacker,
  memory, music player, part loader), `demo.c` is the demo script
  (the part order of MAIN/U2.ASM).
- `src/parts/NAME`: one directory per part; `gendata.py` converts the
  original data into C headers (in `build/gen`), the part is linked as a
  relocatable image against the kernel.
- `src/common`: the VISU 3D engine port (U2A, U2E) and helpers.
- `tools`: converters (S3M to Paula register streams, pictures, VISU
  scenes), the packer, the disk image writer, emulator scripts;
  `tools/host` runs the VISU engine natively for testing.

## Music

The 8 channel S3M modules are played by a converter
(`tools/s3m2paula.py`): the songs are emulated (ST3 semantics) and written
as a stream of Paula register writes; the DIS music sync calls
(`dis_musplus`, `dis_sync`, frame counters) are kept.

- Voices to channels (`tools/paula_alloc.py`): whole notes are assigned
  to the 4 channels with lookahead; a note keeps its channel until it ends
  or fades, echo/doubling voices count less, a voice prefers its own
  stereo side.
- MUSIC1 uses note priorities taken from Estrayk's (unfinished) 4 channel
  Protracker arrangement, "second reality 6.mod" (modland.com,
  pub/modules/Protracker/Estrayk): `tools/expert_prio.py` aligns it with
  the S3M and records which notes it keeps (`tools/mus1_prio.json`); the
  rest of the song uses keep rates per channel and sample learnt from it.
- Phase-inverted sample copies (the S3Ms' Dolby Surround trick) are played
  as the original sample.
- Samples are resampled to fit the budget (MUSIC0 200 KB, MUSIC1 320 KB)
  and normalised, with Paula volume compensating.  The MUSIC1 bank keeps
  the samples heard until late in the song first and is cut to 128 KB at
  order 92, which makes room for the last parts.
- The audio filter ("LED") is switched off.

Software mixer (prototype, off by default): `MIXMASK=8` (5 voices: Paula
channel 3 plays 2 voices mixed by the CPU) or `MIXMASK=12` (6 voices:
channels 2 and 3 each mix 2) when building; `rm -rf build/music` after
changing it.  `src/kernel/mixer.c`/`mixasm.S` mix 128 sample blocks at
8287 Hz from the audio interrupt (at interrupt level 2); the converter
sends mixed voices' events a tick early to make up for the block latency.
Measured on a 68000 (FS-UAE, cycle exact, idle loop count with the
`test` part as CPU meter): 5 voices cost 16.6% of the CPU, 6 voices 31.6%
(the inner loop alone ~28%).  `s3m2paula.py --mixmask N --wav` renders
what the Amiga plays, mixer included.

`tools/musdiff.py` renders the original 8 voice mix and the conversion and
reports where they differ (`--wav` writes both).

## The parts## The parts

Everything is timed by the music like the original; effects that ran at
70 frames per second on VGA are scaled to the Amiga's 50.

| part | Amiga version |
|------|---------------|
| ALKU | horizon pictures and the scrolling texts |
| U2A | VISU engine: ships over the horizon (slow on heavy frames) |
| PAM | the explosion animation, unpacked band by band |
| BEGLOGO | title picture from white |
| GLENZ | zoomer, plate, glenz vectors (blitter XOR fill) |
| TUNNELI | dot tunnel (asm plotter) |
| TECHNO | interference circles, beat columns, bars, the troll |
| PANICEND | the dot and the fade |
| MNTSCRL | text lighting up the landscape (1 plane mapper) |
| LNS&ZOOM | reveal, lens (2x2 cells, movep C2P, blitter cookie cut), rotozoomer (4x4 cells), 25 fps |
| PLZPART | copper plasma (40x70 cells of 12 bit colour, the two plasmas mixed by a checkerboard plane), textured plasma cube (5 planes, 10 shades per face colour) |
| MINVBALL | 384 bouncing balls with shadows, copper floor |
| RAYSCRL | sword reflections as a tint plane over the raytraced picture |
| 3DSINFLD | voxel waves: copper chunky 40x70, ray march generated from DOLOOP.C |
| JPLOGO | the picture at 7 widths, copper modulos do the bounce and bulge |
| U2E | VISU engine: the city |
| ENDLOGO | Future Crew picture |
| CRED | the 21 credit screens |
| ENDSCRL | hires 2 plane end scroller |
