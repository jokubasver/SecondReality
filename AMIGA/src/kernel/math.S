| 32 bit multiply / divide helpers for gcc on a plain 68000
| (libgcc's versions use bsr.l, a 68020 instruction).

	.text
	.globl	__mulsi3, __udivsi3, __divsi3, __umodsi3, __modsi3

__mulsi3:
	move.w	4(sp),d0		| a.hi
	mulu.w	10(sp),d0		| a.hi * b.lo
	move.w	6(sp),d1		| a.lo
	mulu.w	8(sp),d1		| a.lo * b.hi
	add.w	d1,d0
	swap	d0
	clr.w	d0
	move.w	6(sp),d1
	mulu.w	10(sp),d1		| a.lo * b.lo
	add.l	d1,d0
	rts

| unsigned d0 = d0 / d1  (d1 != 0); uses d2
udiv32:
	move.l	d2,-(sp)
	cmp.l	#0x10000,d1
	bcc.s	2f
	move.l	d0,d2
	clr.w	d2
	swap	d2
	divu.w	d1,d2			| high word
	move.w	d2,a0			| save quotient high (remainder stays in the high word)
	move.w	d0,d2
	divu.w	d1,d2			| (rem:low) / d1
	move.w	a0,d0
	swap	d0
	move.w	d2,d0
	move.l	(sp)+,d2
	rts
2:	| divisor >= 65536: shift both until divisor fits, then correct
	move.l	d1,-(sp)		| original divisor
	move.l	d0,-(sp)		| original dividend
	move.l	d1,d2
3:	lsr.l	#1,d1
	lsr.l	#1,d0
	cmp.l	#0x10000,d1
	bcc.s	3b
	divu.w	d1,d0
	and.l	#0xffff,d0		| estimated quotient (may be one too big)
	move.l	d2,d1
	mulu.w	d0,d1			| q * div.lo
	swap	d2
	mulu.w	d0,d2			| q * div.hi
	swap	d2
	tst.w	d2
	bne.s	4f
	add.l	d2,d1
	bcs.s	4f
	cmp.l	(sp),d1
	bls.s	5f
4:	subq.l	#1,d0
5:	addq.l	#8,sp
	move.l	(sp)+,d2
	rts

__udivsi3:
	move.l	4(sp),d0
	move.l	8(sp),d1
	bra.s	udiv32

__divsi3:
	move.l	d2,-(sp)
	moveq	#0,d2
	move.l	8(sp),d0
	bpl.s	1f
	neg.l	d0
	not.b	d2
1:	move.l	12(sp),d1
	bpl.s	2f
	neg.l	d1
	not.b	d2
2:	bsr	udiv32
	tst.b	d2
	beq.s	3f
	neg.l	d0
3:	move.l	(sp)+,d2
	rts

__umodsi3:
	move.l	4(sp),d0
	move.l	8(sp),d1
	bsr	udiv32
	move.l	8(sp),d1
	move.l	d0,-(sp)
	move.l	d1,-(sp)
	bsr	__mulsi3
	addq.l	#8,sp
	move.l	4(sp),d1
	sub.l	d0,d1
	move.l	d1,d0
	rts

__modsi3:
	move.l	d2,-(sp)
	move.l	8(sp),d0
	move.l	d0,d2			| sign of result = sign of dividend
	bpl.s	1f
	neg.l	d0
1:	move.l	12(sp),d1
	bpl.s	2f
	neg.l	d1
2:	move.l	d1,-(sp)
	move.l	d0,-(sp)
	bsr	udiv32			| d0 = |a| / |b|
	move.l	4(sp),d1
	move.l	d0,-(sp)
	move.l	d1,-(sp)
	bsr	__mulsi3
	addq.l	#8,sp
	move.l	(sp)+,d1		| |a|
	addq.l	#4,sp
	sub.l	d0,d1
	move.l	d1,d0
	tst.l	d2
	bpl.s	3f
	neg.l	d0
3:	move.l	(sp)+,d2
	rts
