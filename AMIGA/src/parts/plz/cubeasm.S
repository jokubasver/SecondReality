| PLZPART plasma cube: span texturer and 5 plane C2P.

	.text

| void cube_span(u8 *dst, int n, u32 u, u32 v, u32 du, s32 dv,
|                const u8 *tex, const u16 *sh, int add)
|   (u, v, du, dv are loaded with movem into d2-d5 in this order)
|   u/du: swapped 16.16 (fraction in the high word, integer in the low);
|   v/dv: 8.8 in the low word.  Texel index = (v_int << 8 | u_int) +
|   sh[v_int * 128] (the row's wave offset); out = tex[index] + add.
	.globl	cube_span
cube_span:
	movem.l	d2-d7/a2-a3,-(sp)
	move.l	36(sp),a1
	move.l	40(sp),d0
	movem.l	44(sp),d2-d5
	move.l	60(sp),a0
	move.l	64(sp),a2
	move.l	68(sp),d6
	moveq	#0,d7
	subq.l	#1,d0
	bmi	.Lsend
	cmp.w	#79,d0
	bls.s	1f
	moveq	#79,d0
1:	addq.w	#1,d0
	mulu	#30,d0
	lea	.Lsl(pc),a3
	sub.l	d0,a3
	jmp	(a3)
	.rept	80
	move.w	d3,d1
	move.b	d2,d1
	move.w	d1,d0
	clr.b	d0
	add.w	0(a2,d0.w),d1
	move.b	0(a0,d1.w),d0
	add.b	d6,d0
	and.b	#0x7c,d0
	move.b	d0,(a1)+
	add.l	d4,d2
	addx.w	d7,d2
	add.w	d5,d3
	.endr
.Lsl:
.Lsend:	movem.l	(sp)+,d2-d7/a2-a3
	rts

| void cube_c2p(const u8 *chunky, u8 *tmp, u8 *p4, int ngroups, int nrows,
|               const u8 *lt, const u8 *bt)
|   chunky: 80 bytes per row (values c*4, c < 32); tmp: word interleaved
|   planes 0-3, 160 bytes per row; p4: plane 4, 40 bytes per row.
|   lt/bt point 128 bytes into the long/byte tables (two positions).
	.globl	cube_c2p
cube_c2p:
	movem.l	d2-d7/a2-a5,-(sp)
	move.l	44(sp),a0
	move.l	48(sp),a3
	move.l	52(sp),a5
	move.l	56(sp),d6
	move.l	60(sp),d7
	move.l	64(sp),a2
	move.l	68(sp),a4
	subq.w	#1,d6
	bmi.s	.Lcend
	subq.w	#1,d7
	bmi.s	.Lcend
	moveq	#80,d5
	sub.w	d6,d5
	sub.w	d6,d5
	sub.w	d6,d5
	sub.w	d6,d5
	subq.w	#4,d5			| chunky skip per row
	moveq	#0,d1
.Lcr:	move.w	d6,d3
	move.l	a3,-(sp)
	move.l	a5,-(sp)
.Lcg:
	.irp	o,0,1
	move.b	(a0)+,d1
	move.l	-128(a2,d1.w),d0
	move.b	-128(a4,d1.w),d4
	move.b	(a0)+,d1
	or.l	0(a2,d1.w),d0
	or.b	0(a4,d1.w),d4
	movep.l	d0,\o(a3)
	move.b	d4,(a5)+
	.endr
	addq.l	#8,a3
	dbra	d3,.Lcg
	move.l	(sp)+,a5
	move.l	(sp)+,a3
	lea	40(a5),a5
	lea	160(a3),a3
	add.w	d5,a0
	dbra	d7,.Lcr
.Lcend:	movem.l	(sp)+,d2-d7/a2-a5
	rts
