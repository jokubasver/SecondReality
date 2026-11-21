| PLZPART copper plasma: one row into two copper lines.
|
| void plz_even(const u16 *p1, const s32 *ta, u16 *cop, const s16 *tb,
|               const u16 *p2, const u16 *pal)   (cells 0, 2, .. 38)
| void plz_odd(...)                               (cells 1, 3, .. 39)
|   (argument order = register order a0, a1, a3, a4, a5, a6 for movem)
|   value = p1[ta[-8x]] + p2[tb[96x]] (byte offsets), colour = pal[value]
|   cop points at a line slot (LSLOT bytes); cell x's colour word is at
|   4x + 10, the next line's slot gets the same colours.

LSLOT = 180

	.macro	CELLS start
	.set	x,\start
	.rept	20
	move.l	-32*x(a1),d0
	move.w	0(a0,d0.l),d2
	move.w	192*x(a4),d1
	add.w	0(a5,d1.w),d2
	move.w	0(a6,d2.w),d3
	move.w	d3,(4*x+10)(a3)
	move.w	d3,(4*x+10+LSLOT)(a3)
	.set	x,x+2
	.endr
	.endm

	.text
	.globl	plz_even, plz_odd
plz_even:
	movem.l	d2-d3/a2-a6,-(sp)
	movem.l	32(sp),a0-a1/a3-a6
	CELLS	0
	movem.l	(sp)+,d2-d3/a2-a6
	rts

plz_odd:
	movem.l	d2-d3/a2-a6,-(sp)
	movem.l	32(sp),a0-a1/a3-a6
	CELLS	1
	movem.l	(sp)+,d2-d3/a2-a6
	rts
