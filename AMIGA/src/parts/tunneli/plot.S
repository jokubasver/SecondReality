| Dot ring plotter for TUNNELI.
| void tun_ring(const s16 *pts, int dx, int dy40, u8 *plane, int mask)
|   pts : 64 points of (x, y*40); plane: first of 4 planes, 8000 bytes apart
|   mask: which planes get the dot (bits 0..3)

	.text
	.globl	tun_ring
tun_ring:
	movem.l	d2-d7/a2-a5,-(sp)
	move.l	44(sp),a0		| pts
	move.l	48(sp),d5		| dx
	move.l	52(sp),d6		| dy*40
	move.l	56(sp),a1		| plane 0
	move.l	60(sp),d7		| mask
	lea	8000(a1),a2
	lea	16000(a1),a3
	lea	24000(a1),a4
	move.w	#319,d3
	move.w	#199*40,d4
	moveq	#63,d2
	| select the loop for the plane mask
	and.w	#15,d7
	add.w	d7,d7
	move.w	.Ljt(pc,d7.w),d7
	jmp	.Ljt(pc,d7.w)
.Ljt:
	.word	.Ldone-.Ljt, .Lm1-.Ljt, .Lm2-.Ljt, .Lm3-.Ljt
	.word	.Lm4-.Ljt, .Lm5-.Ljt, .Lm6-.Ljt, .Lm7-.Ljt
	.word	.Lm8-.Ljt, .Lm9-.Ljt, .Lm10-.Ljt, .Lm11-.Ljt
	.word	.Lm12-.Ljt, .Lm13-.Ljt, .Lm14-.Ljt, .Lm15-.Ljt

| one point: d0 = bit, d1 = byte offset; clipped points jump to 7f / 8f
.macro	POINT
	move.w	(a0)+,d0
	add.w	d5,d0
	cmp.w	d3,d0
	bhi.s	7f
	move.w	(a0)+,d1
	add.w	d6,d1
	cmp.w	d4,d1
	bhi.s	8f
	move.w	d0,d7
	lsr.w	#3,d7
	add.w	d7,d1
	not.w	d0
.endm

.macro	LOOP name, p0, p1, p2, p3
\name:
1:	POINT
	.if \p0
	bset	d0,0(a1,d1.w)
	.endif
	.if \p1
	bset	d0,0(a2,d1.w)
	.endif
	.if \p2
	bset	d0,0(a3,d1.w)
	.endif
	.if \p3
	bset	d0,0(a4,d1.w)
	.endif
	dbf	d2,1b
	bra	.Ldone
7:	addq.l	#2,a0
8:	dbf	d2,1b
	bra	.Ldone
.endm

	LOOP	.Lm1, 1,0,0,0
	LOOP	.Lm2, 0,1,0,0
	LOOP	.Lm3, 1,1,0,0
	LOOP	.Lm4, 0,0,1,0
	LOOP	.Lm5, 1,0,1,0
	LOOP	.Lm6, 0,1,1,0
	LOOP	.Lm7, 1,1,1,0
	LOOP	.Lm8, 0,0,0,1
	LOOP	.Lm9, 1,0,0,1
	LOOP	.Lm10, 0,1,0,1
	LOOP	.Lm11, 1,1,0,1
	LOOP	.Lm12, 0,0,1,1
	LOOP	.Lm13, 1,0,1,1
	LOOP	.Lm14, 0,1,1,1
	LOOP	.Lm15, 1,1,1,1
.Ldone:
	movem.l	(sp)+,d2-d7/a2-a5
	rts
