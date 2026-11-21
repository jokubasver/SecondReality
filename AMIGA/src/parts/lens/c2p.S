| LENS/ZOOM chunky to planar helpers.
|
| Both write a "word interleaved" buffer: for every 16 pixels, one word of
| each of the 4 planes in a row (movep.l writes the plane bytes at stride 2).
| The blitter then pulls out each plane with a width 1 blit (A modulo 6).
|
| Tables: 16 longs per cell position, plane 0 in the high byte, index c*4.
| a2 points 128 bytes into the table block.

	.text

| void zoom_c2p(const u8 *texmid, const u16 *rowbase, const u8 *tab, u8 *tmp, int nrows)
|   nrows rows of 80 cells (4 pixels wide, 2 per plane byte); 160 bytes per
|   row out.  The source displacements in zoom_code are patched per frame:
|   texel = texmid[rowbase + disp] where texmid = texture + 32768 (the
|   texture is stored twice so rowbase + offset never has to wrap).
	.globl	zoom_c2p, zoom_code
zoom_c2p:
	movem.l	d2-d7/a2-a4,-(sp)
	move.l	40(sp),a4
	move.l	44(sp),a1
	move.l	48(sp),a2
	move.l	52(sp),a3
	move.l	56(sp),d7
	subq.w	#1,d7
	moveq	#0,d1
.Lzrow:	moveq	#0,d2
	move.w	(a1)+,d2
	lea	0(a4,d2.l),a0
zoom_code:
	.set	k,0
	.rept	40
	move.b	0x1234(a0),d1
	move.l	-128(a2,d1.w),d0
	move.b	0x1234(a0),d1
	or.l	-64(a2,d1.w),d0
	movep.l	d0,((k>>1)*8+(k&1))(a3)
	.set	k,k+1
	.endr
	lea	160(a3),a3
	dbra	d7,.Lzrow
	movem.l	(sp)+,d2-d7/a2-a4
	rts

| void lens_run(const u8 *src, const u16 *code, const u16 *rows, const u8 *tab, u8 *tmp, int nrows)
|   calls the generated row routines; tmp points at the first row's output
	.globl	lens_run
lens_run:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a0
	move.l	52(sp),a5
	move.l	56(sp),a1
	move.l	60(sp),a2
	move.l	64(sp),a3
	move.l	68(sp),d7
	subq.w	#1,d7
	bmi.s	.Lrend
	moveq	#0,d1
.Lrr:	moveq	#0,d2
	move.w	(a1)+,d2
	lea	0(a5,d2.l),a4
	jsr	(a4)
	lea	80(a3),a3
	dbra	d7,.Lrr
.Lrend:	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void zoom_patch(u32 u, u32 v, s32 du, s32 dv)
|   writes the 80 column offsets ((v >> 16) << 8 | (u >> 16) & 255, xor
|   0x8000) into the displacements of zoom_code; u/v step by du/dv per cell
	.globl	zoom_patch
zoom_patch:
	movem.l	d2-d5,-(sp)
	movem.l	20(sp),d0-d3
	lea	zoom_code+2,a0
	moveq	#39,d5
.Lzp:
	.irp	o,0,8
	move.l	d1,d4
	swap	d4
	lsl.w	#8,d4
	swap	d0
	move.b	d0,d4
	swap	d0
	eor.w	#0x8000,d4
	move.w	d4,\o(a0)
	add.l	d2,d0
	add.l	d3,d1
	.endr
	lea	20(a0),a0
	dbra	d5,.Lzp
	movem.l	(sp)+,d2-d5
	rts

| void zoom_rows(u32 x, u32 y, s32 dx, s32 dy, u16 *out, int n)
|   out[i] = ((y >> 16) & 255) << 8 | ((x >> 16) & 255), x/y stepped first
	.globl	zoom_rows
zoom_rows:
	movem.l	d2-d5,-(sp)
	movem.l	20(sp),d0-d3
	move.l	36(sp),a0
	move.l	40(sp),d5
	subq.w	#1,d5
.Lzr:	add.l	d3,d1
	add.l	d2,d0
	move.l	d1,d4
	swap	d4
	lsl.w	#8,d4
	swap	d0
	move.b	d0,d4
	swap	d0
	move.w	d4,(a0)+
	dbra	d5,.Lzr
	movem.l	(sp)+,d2-d5
	rts
