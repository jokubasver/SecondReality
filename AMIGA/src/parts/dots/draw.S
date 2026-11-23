| MINVBALL: project and draw the balls and their shadows.
|
| void dots_draw(const s16 *dots, int n, u8 *buf, int rotsin, int rotcos,
|                const s16 *inv, const u32 *pat, const u32 *rowoff,
|                const u8 *lev)
|   dots: x, y, z, yadd per ball.  buf: interleaved planes, 42 bytes per
|   plane row, 210 per row.  pat: 16 longs each for the ball's narrow rows
|   (.XX.), the wide row (XXXX) and the shadow (XX.), by x & 15;
|   pat points at the second table.
|   lev[bp >> 8]: depth shade + 1 (bits for planes 1-4); plane 0 holds the
|   ball centres and the shadows.

ROW = 210
PLN = 42

	.text
	.globl	dots_draw
dots_draw:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a1
	move.l	52(sp),d0
	move.l	56(sp),a0
	move.l	60(sp),d6
	move.l	64(sp),d7
	move.l	68(sp),a2
	move.l	72(sp),a4
	move.l	76(sp),a5
	move.l	80(sp),a6
	subq.w	#1,d0
	bmi	.Lend
	move.w	d0,-(sp)
.Lloop:
	move.w	(a1),d0			| x
	move.w	4(a1),d1		| z
	move.w	d1,d2
	muls	d7,d2			| z * cos
	move.w	d0,d3
	muls	d6,d3			| x * sin
	swap	d2
	swap	d3
	sub.w	d3,d2
	add.w	#9000,d2		| bp
	muls	d7,d0			| x * cos
	muls	d6,d1			| z * sin
	add.l	d1,d0
	asr.l	#8,d0
	move.l	d0,d1
	asr.l	#3,d1
	add.l	d1,d0			| * 9/8
	move.w	d2,d3
	lsr.w	#2,d3
	add.w	d3,d3
	move.w	0(a2,d3.w),d3		| inv = 2^22 / bp
	asr.l	#7,d0
	muls	d3,d0
	add.l	d0,d0
	swap	d0
	add.w	#160,d0			| X
	cmp.w	#316,d0
	bhi	.Lnext
	move.w	d0,d1
	and.w	#15,d1
	lsl.w	#2,d1			| pattern offset
	lsr.w	#4,d0
	add.w	d0,d0			| word offset
	| shadow
	move.w	d3,d4
	lsr.w	#3,d4
	add.w	#100,d4
	cmp.w	#199,d4
	bhi	.Lnext
	add.w	d4,d4
	add.w	d4,d4
	move.l	0(a5,d4.w),a3
	add.l	a0,a3
	add.w	d0,a3
	move.l	64(a4,d1.w),d4
	or.l	d4,(a3)
	| ball
	move.w	2(a1),d5
	muls	d3,d5
	swap	d5
	add.w	#100,d5
	cmp.w	#199,d5
	bhi	.Lnext
	add.w	d5,d5
	add.w	d5,d5
	move.l	0(a5,d5.w),a3
	add.l	a0,a3
	add.w	d0,a3
	move.l	-64(a4,d1.w),d4
	move.l	0(a4,d1.w),d5
	or.l	d4,ROW(a3)
	lsr.w	#8,d2
	move.b	0(a6,d2.w),d2
	.irp	p,1,2,3,4
	btst	#\p-1,d2
	beq.s	1f
	or.l	d4,PLN*\p(a3)
	or.l	d5,ROW+PLN*\p(a3)
	or.l	d4,2*ROW+PLN*\p(a3)
1:
	.endr
.Lnext:	addq.l	#8,a1
	subq.w	#1,(sp)
	bpl	.Lloop
	addq.l	#2,sp
.Lend:	movem.l	(sp)+,d2-d7/a2-a6
	rts

| void dots_phys(s16 *dots, int count, const u8 *spawn, int n, int gravity, int gravd)
|   gravity and floor bounces for n 70 Hz phases; a ball dropped in phase
|   spawn[b] (1..n) gets n + 1 - spawn[b] steps, the others n
	.globl	dots_phys
dots_phys:
	movem.l	d2-d7/a2,-(sp)
	move.l	32(sp),a0		| dots
	move.l	36(sp),d7		| count
	move.l	40(sp),a1		| spawn
	move.l	44(sp),d6		| n
	move.l	48(sp),d4		| gravity
	move.l	52(sp),d5		| gravd
	subq.w	#1,d7
	bmi.s	9f
	move.w	#8105,a2
1:	move.w	d6,d3			| steps
	moveq	#0,d0
	move.b	(a1)+,d0
	beq.s	2f
	subq.w	#1,d0
	sub.w	d0,d3
	beq.s	5f
2:	move.w	2(a0),d1		| y
	move.w	6(a0),d0		| yadd
	subq.w	#1,d3
3:	add.w	d4,d0
	add.w	d0,d1
	cmp.w	a2,d1
	bge.s	6f
4:	dbf	d3,3b
	move.w	d1,2(a0)
	move.w	d0,6(a0)
5:	addq.l	#8,a0
	dbf	d7,1b
9:	movem.l	(sp)+,d2-d7/a2
	rts
6:	neg.w	d0			| bounce: yadd = (s16)(-yadd * gravd) >> 4
	muls	d5,d0
	asr.w	#4,d0
	add.w	d0,d1
	bra.s	4b
