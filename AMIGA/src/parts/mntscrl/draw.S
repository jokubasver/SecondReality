| void mnt_draw(const u16 *map, const u32 *textcols, int k, u8 *plane)
|   map: per font pixel (y*237+x): count, then count words of (offset<<3 | bit)
|   the font pixel is bit y of textcols[clamp(x + k - 104, 0..639)] (0 if < 0)

	.text
	.globl	mnt_draw
mnt_draw:
	movem.l	d2-d7/a2-a4,-(sp)
	move.l	40(sp),a0		| map
	move.l	44(sp),a2		| textcols
	move.l	48(sp),d6		| k
	move.l	52(sp),a1		| plane
	sub.w	#104,d6			| column of x = 0
	moveq	#0,d7			| y
.Ly:	move.w	#236,d5			| x counter
	move.w	d6,d4			| column
.Lx:	move.w	(a0)+,d3		| count
	beq.s	.Lnext
	| font bit
	moveq	#0,d2
	tst.w	d4
	bmi.s	.Loff
	move.w	d4,d0
	cmp.w	#639,d0
	bls.s	1f
	move.w	#639,d0
1:	add.w	d0,d0
	add.w	d0,d0
	move.l	0(a2,d0.w),d0
	btst	d7,d0
	beq.s	.Loff
	subq.w	#1,d3
.Lon:	move.w	(a0)+,d0
	move.w	d0,d1
	lsr.w	#3,d1
	bset	d0,0(a1,d1.w)
	dbf	d3,.Lon
	bra.s	.Lnext
.Loff:	subq.w	#1,d3
.Lo2:	move.w	(a0)+,d0
	move.w	d0,d1
	lsr.w	#3,d1
	bclr	d0,0(a1,d1.w)
	dbf	d3,.Lo2
.Lnext:	addq.w	#1,d4
	dbf	d5,.Lx
	addq.w	#1,d7
	cmp.w	#31,d7
	bne.s	.Ly
	movem.l	(sp)+,d2-d7/a2-a4
	rts
