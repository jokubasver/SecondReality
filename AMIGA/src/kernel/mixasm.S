| Software mixer inner loop: 2 voices into one 8 bit Paula buffer.
|
| void mix2(u8 *out, int n, const struct mixv *a, const struct mixv *b)
|
| struct mixv (see mixer.c):
|   0  const u8 *base   sample data at the current integer position
|   4  u32 pos          swapped fixed point: fraction in the high word,
|                       integer offset from base in the low word
|   8  u32 step         same format
|  12  const u8 *tab    volume table row: 256 bytes, ((s8)u * q) >> 6
| pos is written back.  Per output sample: fetch, volume lookup, step
| (add.l steps fraction and integer; addx carries the fraction overflow).

	.text
	.globl	mix2
mix2:
	movem.l	d2-d7/a2-a4,-(sp)
	move.l	40(sp),a4		| out
	move.l	44(sp),d7		| n
	move.l	48(sp),a0		| voice a
	move.l	52(sp),a2		| voice b
	move.l	4(a0),d0
	move.l	8(a0),d1
	move.l	4(a2),d2
	move.l	8(a2),d3
	move.l	12(a0),a1
	move.l	12(a2),a3
	move.l	(a0),a0
	move.l	(a2),a2
	moveq	#0,d4
	moveq	#0,d5
	subq.w	#1,d7
	bmi.s	9f
	| 2x unrolled
	lsr.w	#1,d7
	bcc.s	2f
1:	move.b	(a0,d0.w),d5
	move.b	(a1,d5.w),d6
	add.l	d1,d0
	addx.w	d4,d0
	move.b	(a2,d2.w),d5
	add.b	(a3,d5.w),d6
	add.l	d3,d2
	addx.w	d4,d2
	move.b	d6,(a4)+
2:	move.b	(a0,d0.w),d5
	move.b	(a1,d5.w),d6
	add.l	d1,d0
	addx.w	d4,d0
	move.b	(a2,d2.w),d5
	add.b	(a3,d5.w),d6
	add.l	d3,d2
	addx.w	d4,d2
	move.b	d6,(a4)+
	dbf	d7,1b
9:	move.l	48(sp),a0
	move.l	d0,4(a0)
	move.l	52(sp),a0
	move.l	d2,4(a0)
	movem.l	(sp)+,d2-d7/a2-a4
	rts
