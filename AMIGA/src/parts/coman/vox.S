| 3DSINFLD: ray march one screen column through the wave landscape.
|
| void vox_col(int x, int y, int xs, int ys, u16 *cell, u16 *top,
|              const s16 *w1mid, const s16 *w2mid, const u16 *cols, s32 dir)
|   x/y: byte positions in the wave tables (16 bit, wrapping), xs/ys: steps
|   for the near half (doubled for the far half).  cell: copper colour
|   word of the bottom cell; rows go up by 2 copper lines; top: the
|   column's highest cell.  Cells above the landscape are set to black.

LSLOT = 172

	.text
	.globl	vox_col
vox_col:
	movem.l	d2-d7/a2-a6,-(sp)
	movem.l	48(sp),d0-d1/d4-d5
	move.l	64(sp),a3
	move.l	68(sp),a5
	move.l	72(sp),a1
	move.l	76(sp),a2
	move.l	80(sp),a6
	move.l	84(sp),d7
	add.l	d7,d7			| near: two steps of the ray per step
	moveq	#0,d6
	.include "coman_steps.i"
.Ldone:	cmp.l	a5,a3
	bcs.s	.Lfull
	clr.w	(a3)
	clr.w	LSLOT(a3)
	lea	-2*LSLOT(a3),a3
	bra.s	.Ldone
.Lfull:	movem.l	(sp)+,d2-d7/a2-a6
	rts
