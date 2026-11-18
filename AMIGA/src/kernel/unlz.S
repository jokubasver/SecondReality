| 68000 decoder for the "LZ1" format of tools/lzpack.c (see src/common/unlz.h)
|
| unsigned int unlz_asm(const void *packed, void *dest)
|   d7 = bit buffer (bits are taken from bit 7 with add.b), d6 = end pointer
|   a0 = source, a1 = destination

	.text
	.globl	unlz_asm

| get one bit into the X/C flag
.macro	GETBIT
	add.b	d7,d7
	bne.s	.Lb\@
	move.b	(a0)+,d7
	addx.b	d7,d7
.Lb\@:
.endm

| Elias gamma into d0 (uses d1)
gamma:
	moveq	#0,d1			| leading zero count
1:	GETBIT
	bcs.s	2f
	addq.w	#1,d1
	bra.s	1b
2:	moveq	#1,d0
	subq.w	#1,d1
	bmi.s	4f
3:	GETBIT
	addx.l	d0,d0
	dbf	d1,3b
4:	rts

unlz_asm:
	movem.l	d2-d7/a2-a3,-(sp)
	move.l	36(sp),a0		| packed
	move.l	40(sp),a1		| dest
	move.l	a1,a2			| start (for delta filter)
	move.l	(a0)+,d6
	move.l	d6,d5			| bit 31 = delta flag
	and.l	#0x7fffffff,d6
	beq	.Ldone0
	add.l	a1,d6			| end pointer
	move.b	#0x80,d7		| empty bit buffer (marker bit)
	moveq	#1,d4			| last offset
.Llit:
	bsr.s	gamma			| literal run length
	subq.l	#1,d0
.Lcl:	move.b	(a0)+,(a1)+
	dbf	d0,.Lcl
	sub.l	#0x10000,d0		| runs longer than 65536
	bpl.s	.Lcl
	cmp.l	d6,a1
	bhs.s	.Lend
	GETBIT
	bcs.s	.Lnew
	bsr.s	gamma			| repeat offset, length >= 1
	bra.s	.Lcopy
.Lnew:
	bsr.s	gamma			| high offset part + 1
	subq.l	#1,d0
	lsl.l	#8,d0
	move.b	(a0)+,d0
	addq.l	#1,d0
	move.l	d0,d4			| offset
	bsr.s	gamma
	addq.l	#1,d0			| length >= 2
.Lcopy:
	move.l	a1,a3
	sub.l	d4,a3
	subq.l	#1,d0
.Lcc:	move.b	(a3)+,(a1)+
	dbf	d0,.Lcc
	sub.l	#0x10000,d0
	bpl.s	.Lcc
	cmp.l	d6,a1
	bhs.s	.Lend
	GETBIT
	bcs.s	.Lnew
	bra.s	.Llit
.Lend:
	tst.l	d5
	bpl.s	.Ldone
	| byte delta filter
	moveq	#0,d0
.Ldl:	add.b	(a2),d0
	move.b	d0,(a2)+
	cmp.l	d6,a2
	blo.s	.Ldl
.Ldone:
	move.l	d6,d0
	sub.l	40(sp),d0
	movem.l	(sp)+,d2-d7/a2-a3
	rts
.Ldone0:
	moveq	#0,d0
	movem.l	(sp)+,d2-d7/a2-a3
	rts
