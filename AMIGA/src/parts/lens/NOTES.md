# LENS/ZOOM port - work in progress (not in manifest/build yet)

Only gendata.py exists so far. Original: LENS/MAIN.C + LENS/ASM.ASM.

Data (gendata.py output lens_data.h): pic (4 planes, 16 col), chunky (16 col
indices), pal (16 base + 16 tinted with tint1), lens1..4 tables (s16),
path1 (x,y per frame, PATH1N=715), path2 (x,y,xa,ya, PATH2N=2000).
NOTE: chunky values should become c*4 for the long-table C2P below.

Table formats (word offsets are bytes into the table): per row y (116 rows)
{ptr,count}; at ptr: start (dest offset relative to row u), then
- lens1 (dorow, tint 0x40): count source offsets, dest sequential from start;
  src = back[u+start+off]
- lens2/3 (dorow2, tint 0x80/0xC0): count (dest,src) pairs relative to u+start
- lens4 (dorow3): count offsets: restore back pixel (outside lens)
Lens drawn at x-76,y-58; rows with u<0 or >64000 skipped.

Plan:
- part1: fir-fade reveal, per row left/right edges
  firfade1 = 170*64+(100-y)*50 (+= (23+y/5)&~7), firfade2 same start
  (+= -((23+(199-y)/5)&~7)), 6 steps per frame for 80 frames, 300 frames
  total; start when k_mus_plus() >= -6. Copy revealed bytes from pristine.
- then wait k_mus_plus() >= -20.
- part2 (715 frames): lens in 2x2 cells (76x58), chunky gather with per-cell
  source offset precomputed in gendata, C2P via 4 position long tables +
  movep.l into word-interleaved temp, blitter de-interleave (width 1,
  A mod 6), cookie-cut onto screen with 1x1 lens mask (two blits for row
  doubling), plane 4 = tint mask. Double buffer, restore old rect from
  pristine. Lens fade-in: tinted = tinted - base*(31-x)/31, x=(f-32)/2.
- part3: rotozoomer 80x100 cells (4x2), texture 256x256 from picture
  (row a = y*10/11-18, col x+32), doubled to 128K so idx (base+off)&0xffff
  works with d16 displacements; per pair two long-table lookups + movep,
  de-interleave blits (height <= 1024 per blit), copper line doubling.
  Per pixel u += ya/1024, v -= xa/1024; per row u += xa*307/256/1024,
  v += ya*307/256/1024 (start x,y). Ends at frame 2000 or k_mus_plus() > -4;
  white fade at end (fade table), brightening from +5*(15-f) at start.
- add to manifest disk 2, PARTS, demo.c after MNTSCRL.
