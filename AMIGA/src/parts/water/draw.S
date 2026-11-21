| RAYSCRL: the sword's reflections.
|
| void ray_draw(const u16 *map, const u8 *newm, const u8 *oldm, u8 *plane)
|   map: per scroll buffer pixel (34 rows of 158): count, then count pairs
|   (byte offset, bit).  newm/oldm: the sword mask at the scroll position
|   now and when this map was last drawn (rows of FW bytes).  Only pixels
|   whose mask value changed are written (set or cleared in plane).

FW = 558

	.text
	.globl	ray_draw
ray_draw:
	movem.l	d2-d7/a2-a4,-(sp)
	move.l	40(sp),a0
	move.l	44(sp),a2
	move.l	48(sp),a3
	move.l	52(sp),a1
	moveq	#33,d7
.Lrow:	move.w	#157,d6
	move.l	a2,-(sp)
	move.l	a3,-(sp)
.Lpix:	move.w	(a0)+,d3
	beq.s	.Lnext
	move.b	(a2),d0
	cmp.b	(a3),d0
	beq.s	.Lskip
	subq.w	#1,d3
	tst.b	d0
	beq.s	.Lclr
.Lset:	move.w	(a0)+,d1
	move.w	(a0)+,d2
	bset	d2,0(a1,d1.w)
	dbf	d3,.Lset
	bra.s	.Lnext
.Lclr:	move.w	(a0)+,d1
	move.w	(a0)+,d2
	bclr	d2,0(a1,d1.w)
	dbf	d3,.Lclr
	bra.s	.Lnext
.Lskip:	lsl.w	#2,d3
	add.w	d3,a0
.Lnext:	addq.l	#1,a2
	addq.l	#1,a3
	dbf	d6,.Lpix
	move.l	(sp)+,a3
	move.l	(sp)+,a2
	lea	FW(a2),a2
	lea	FW(a3),a3
	dbf	d7,.Lrow
	movem.l	(sp)+,d2-d7/a2-a4
	rts
