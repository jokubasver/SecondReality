| Edge marking for the VISU engine's blitter fill: one toggled pixel per
| row (the blitter's exclusive fill turns them into spans).
|
| void v3_edge(u8 *row, s32 x, s32 step, int count)
|   row: the scratch plane line of the first row (40 bytes per line)
|   x, step: 16.16 fixed point x and its change per row; x stays within
|   0..319 (the caller makes sure)

	.text
	.globl	v3_edge
v3_edge:
	movem.l	d2-d4,-(sp)
	move.l	16(sp),a0		| row
	movem.l	20(sp),d0-d1/d4		| x step count (d4 last: movem loads d0, d1, d4)
	subq.l	#1,d4
	bmi.s	9f
	moveq	#40,d3
	move.l	d3,a1
1:	move.l	d0,d2
	swap	d2			| x
	move.w	d2,d3
	lsr.w	#3,d3			| byte
	not.b	d2			| bit 7 - (x & 7)
	bchg	d2,0(a0,d3.w)
	add.l	d1,d0
	add.l	a1,a0
	dbf	d4,1b
9:	movem.l	(sp)+,d2-d4
	rts

| void v3_edge_clip(u8 *row, s32 x, s32 step, int count)
|   the same, x clamped to 0..319 (the polygon is cut at the screen sides;
|   x may wrap around in between as long as the true values fit)
	.globl	v3_edge_clip
v3_edge_clip:
	movem.l	d2-d5,-(sp)
	move.l	20(sp),a0
	movem.l	24(sp),d0-d1/d4
	subq.l	#1,d4
	bmi.s	9f
	moveq	#40,d3
	move.l	d3,a1
	move.w	#319,d5
1:	move.l	d0,d2
	swap	d2
	cmp.w	d5,d2
	bls.s	2f			| 0..319
	tst.w	d2
	bmi.s	3f
	move.w	d5,d2
	bra.s	2f
3:	moveq	#0,d2
2:	move.w	d2,d3
	lsr.w	#3,d3
	not.b	d2
	bchg	d2,0(a0,d3.w)
	add.l	d1,d0
	add.l	a1,a0
	dbf	d4,1b
9:	movem.l	(sp)+,d2-d5
	rts

| void v3_span_edge(s16 *spl, s16 *spr, s32 x, s32 step, int count)
|   small polygons: widen the row spans [spl, spr] to the edge's x
	.globl	v3_span_edge
v3_span_edge:
	movem.l	d2-d3,-(sp)
	movem.l	12(sp),a0-a1
	movem.l	20(sp),d0-d1/d3		| x step count
	subq.l	#1,d3
	bmi.s	9f
1:	move.l	d0,d2
	swap	d2
	cmp.w	(a0),d2
	bge.s	2f
	move.w	d2,(a0)
2:	cmp.w	(a1),d2
	ble.s	3f
	move.w	d2,(a1)
3:	addq.l	#2,a0
	addq.l	#2,a1
	add.l	d1,d0
	dbf	d3,1b
9:	movem.l	(sp)+,d2-d3
	rts

| void v3_spans(u8 *row, s32 stride, const s16 *spl, const s16 *spr, int count, int color)
|   small polygons: fill the row spans into the 5 planes (stride apart),
|   row = the first row of plane 0, spl/spr = its span
	.globl	v3_spans
v3_spans:
	movem.l	d2-d7/a2-a4,-(sp)
	movem.l	40(sp),a0/a1		| row, stride (a1)
	movem.l	48(sp),a2-a3		| spl, spr
	move.l	56(sp),d7		| count
	move.l	60(sp),d6		| colour
	subq.l	#1,d7
	bmi	99f
.Lrow:	move.w	(a2)+,d0		| xl
	move.w	(a3)+,d1		| xr
	tst.w	d0
	bpl.s	1f
	moveq	#0,d0
1:	cmp.w	#319,d1
	ble.s	2f
	move.w	#319,d1
2:	cmp.w	d1,d0
	bgt	.Lnext
	move.w	d0,d2
	lsr.w	#4,d2			| w0
	move.w	d1,d3
	lsr.w	#4,d3
	sub.w	d2,d3			| words after the first
	add.w	d2,d2
	lea	0(a0,d2.w),a4		| first word, plane 0
	moveq	#-1,d4
	and.w	#15,d0
	lsr.w	d0,d4			| first word mask
	moveq	#-1,d5
	not.w	d1
	and.w	#15,d1
	lsl.w	d1,d5			| last word mask
	move.b	d6,d1			| colour bits
	moveq	#4,d2			| planes
	tst.w	d3
	bne.s	.Lwide
	and.w	d5,d4			| one word
	move.w	d4,d5
	not.w	d5
.L1p:	lsr.b	#1,d1
	bcc.s	3f
	or.w	d4,(a4)
	bra.s	4f
3:	and.w	d5,(a4)
4:	add.l	a1,a4
	dbf	d2,.L1p
	bra.s	.Lnext
.Lwide:	subq.w	#1,d3			| middle words
.Lwp:	move.l	a4,-(sp)
	move.w	d3,d0
	lsr.b	#1,d1
	bcc.s	.Lclr
	or.w	d4,(a4)+
	bra.s	6f
5:	move.w	#-1,(a4)+
6:	dbf	d0,5b
	or.w	d5,(a4)
	bra.s	.Lwn
.Lclr:	not.w	d4
	not.w	d5
	and.w	d4,(a4)+
	bra.s	8f
7:	clr.w	(a4)+
8:	dbf	d0,7b
	and.w	d5,(a4)
	not.w	d4
	not.w	d5
.Lwn:	move.l	(sp)+,a4
	add.l	a1,a4
	dbf	d2,.Lwp
.Lnext:	lea	40(a0),a0
	dbf	d7,.Lrow
99:	movem.l	(sp)+,d2-d7/a2-a4
	rts
