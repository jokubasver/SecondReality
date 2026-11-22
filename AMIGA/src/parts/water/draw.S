| RAYSCRL: the sword's reflections.
|
| void ray_draw(const u16 *map, const u8 *newm, const u8 *oldm, u8 *screen)
|   map: per scroll buffer pixel (34 rows of 158): count, then count pairs
|   (byte offset, bit | background colour << 8).  newm/oldm: the sword
|   strip (0 or colour 16-31) at the scroll position now and when this map
|   was last drawn (rows of FW bytes).  Where a pixel's colour changed, the
|   screen pixels it maps to get the bits that differ flipped in all 5
|   planes (colour = the sword's, or the background's where it has gone).

PL = 8000
	.include "water_fw.i"

	.text
	.globl	ray_draw
ray_draw:
	movem.l	d2-d7/a2-a5,-(sp)
	move.l	44(sp),a0
	move.l	48(sp),a2
	move.l	52(sp),a3
	move.l	56(sp),a1
	moveq	#33,d7
.Lrow:	move.w	#157,d6
	move.l	a2,-(sp)
	move.l	a3,-(sp)
.Lpix:	move.w	(a0)+,d3
	beq.s	.Lnext
	move.b	(a2),d0			| new colour (0 = none)
	move.b	(a3),d1			| old colour
	cmp.b	d0,d1
	beq.s	.Lskip
	subq.w	#1,d3
.Lpos:	move.w	(a0)+,d4		| byte offset
	lea	0(a1,d4.w),a5
	move.w	(a0)+,d2		| bit | background << 8
	move.w	d2,d5
	lsr.w	#8,d5			| d5 = background colour
	move.b	d1,d4			| old: sword or background
	bne.s	1f
	move.b	d5,d4
1:	tst.b	d0			| new: sword or background
	beq.s	2f
	move.b	d0,d5
2:	eor.b	d4,d5			| bits that differ
	beq.s	9f
	lsr.b	#1,d5
	bcc.s	3f
	bchg	d2,(a5)
3:	lsr.b	#1,d5
	bcc.s	4f
	bchg	d2,PL(a5)
4:	lsr.b	#1,d5
	bcc.s	5f
	bchg	d2,2*PL(a5)
5:	lsr.b	#1,d5
	bcc.s	6f
	bchg	d2,3*PL(a5)
6:	lsr.b	#1,d5
	bcc.s	9f
	bchg	d2,4*PL(a5)
9:	dbf	d3,.Lpos
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
	movem.l	(sp)+,d2-d7/a2-a5
	rts
