| PLZPART copper plasma: one line.
|
| void plz_line(const u16 *p1, const s32 *ta, u16 *cop, const s16 *tb,
|               const u16 *p2, const u16 *pal)
|   (argument order = register order a0, a1, a3, a4, a5, a6 for movem)
|   value = p1[ta[-8x]] + p2[tb[96x]] (byte offsets), colour = pal[value]
|   for the 40 cells x of the line; cop points at the line slot, cell x's
|   colour word is at 4x + 6 (after the line's WAIT and the register word).

	.macro	CELLS
	.set	x,0
	.rept	40
	move.l	-32*x(a1),d0
	move.w	0(a0,d0.l),d2
	move.w	192*x(a4),d1
	add.w	0(a5,d1.w),d2
	move.w	0(a6,d2.w),(4*x+6)(a3)
	.set	x,x+1
	.endr
	.endm

	.text
	.globl	plz_line
plz_line:
	movem.l	d2/a2-a6,-(sp)
	movem.l	28(sp),a0-a1/a3-a6
	CELLS
	movem.l	(sp)+,d2/a2-a6
	rts
