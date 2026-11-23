| void mnt_draw(const u16 *map, const u32 *newc, const u32 *oldc, u8 *plane)
|   map: per text column (237): byte length of its entries, their number
|   - 1, then the entries (row, 2 * count, count words of byte offset << 3
|   | bit).  newc/oldc: the text columns (bit = row) at the scroll
|   position now and when this map was last drawn.  Only the pixels whose
|   font bit changed are written.

	.text
	.globl	mnt_draw
mnt_draw:
	movem.l	d2-d7/a2-a3,-(sp)
	move.l	36(sp),a0		| map
	move.l	40(sp),a2		| new columns
	move.l	44(sp),a3		| old columns
	move.l	48(sp),a1		| plane
	move.w	#236,d7
.Lcol:	move.l	(a2)+,d2
	move.l	(a3)+,d1
	eor.l	d2,d1			| rows that changed
	bne.s	.Lchg
	move.w	(a0)+,d3		| skip the count word and entries
	addq.l	#2,a0
	add.w	d3,a0
	dbf	d7,.Lcol
	bra.s	.Lend
.Lchg:	addq.l	#2,a0
	move.w	(a0)+,d6		| entries - 1
	bmi.s	.Lnext
.Lent:	move.w	(a0)+,d4		| row
	move.w	(a0)+,d3		| 2 * count
	btst	d4,d1
	bne.s	.Lput
	add.w	d3,a0
	dbf	d6,.Lent
	dbf	d7,.Lcol
	bra.s	.Lend
.Lput:	lsr.w	#1,d3
	subq.w	#1,d3
	btst	d4,d2
	beq.s	.Loff
.Lon:	move.w	(a0)+,d0
	move.w	d0,d5
	lsr.w	#3,d5
	bset	d0,0(a1,d5.w)
	dbf	d3,.Lon
	dbf	d6,.Lent
	dbf	d7,.Lcol
	bra.s	.Lend
.Loff:	move.w	(a0)+,d0
	move.w	d0,d5
	lsr.w	#3,d5
	bclr	d0,0(a1,d5.w)
	dbf	d3,.Loff
	dbf	d6,.Lent
.Lnext:	dbf	d7,.Lcol
.Lend:	movem.l	(sp)+,d2-d7/a2-a3
	rts
