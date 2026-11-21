| Vertex transform + perspective projection for the VISU engine port.
|
| void v3_xform_idx(const s16 *v, const u16 *idx, int n, const struct xf *xf, struct vout *out)
|   v   : x,y,z words per vertex (object space, scaled)
|   idx : vertex numbers to transform (out is indexed by vertex number)
|   xf  : s16 m[9]  rows 0/1 premultiplied by mulx/256 and muly/256
|         s16 pad; s32 tx,ty,tz (20) with tx,ty premultiplied by mulx/muly;
|         s32 mulx (32), muly (36), addx (40), addy (44), zmin (48)
|   out : per vertex: s32 x*mulx, s32 y*muly, s32 z, s16 sx, s16 sy (16 bytes)
|         sx/sy are only valid when z >= zmin
|
| The rows 0/1 (premultiplied by mulx/256) products are >>6 (8 + 6 = 14),
| so x and y come out already multiplied by the projection factors.

	.text
	.globl	v3_xform_idx

| d = d >> 14 (d holds a sum of 16x16 products), 20 cycles
.macro	SHR14 r
	asl.l	#2,\r
	swap	\r
	ext.l	\r
.endm

v3_xform_idx:
	movem.l	d2-d7/a2-a6,-(sp)
	move.l	48(sp),a3		| v
	move.l	52(sp),a4		| idx
	move.l	56(sp),d7		| n
	move.l	60(sp),a1		| xf
	move.l	64(sp),a5		| out
	subq.l	#1,d7
	bmi	9f
1:
	move.w	(a4)+,d0		| vertex index
	move.w	d0,d1
	add.w	d0,d0
	add.w	d1,d0			| *3
	add.w	d0,d0			| *6 bytes
	lea	0(a3,d0.w),a0
	lsl.w	#4,d1			| *16 bytes
	lea	0(a5,d1.w),a2
	movem.w	(a0),d0-d2		| x y z (sign extended)
	| ---- z
	move.w	d0,d5
	muls.w	12(a1),d5
	move.w	d1,d6
	muls.w	14(a1),d6
	add.l	d6,d5
	move.w	d2,d6
	muls.w	16(a1),d6
	add.l	d6,d5
	SHR14	d5
	add.l	28(a1),d5		| + tz
	| ---- x * mulx
	move.w	d0,d3
	muls.w	(a1),d3
	move.w	d1,d6
	muls.w	2(a1),d6
	add.l	d6,d3
	move.w	d2,d6
	muls.w	4(a1),d6
	add.l	d6,d3
	asr.l	#6,d3
	add.l	20(a1),d3		| + tx * mulx
	| ---- y * muly
	move.w	d0,d4
	muls.w	6(a1),d4
	move.w	d1,d6
	muls.w	8(a1),d6
	add.l	d6,d4
	move.w	d2,d6
	muls.w	10(a1),d6
	add.l	d6,d4
	asr.l	#6,d4
	add.l	24(a1),d4		| + ty * muly
	movem.l	d3-d5,(a2)
	| ---- projection: sx = x*mulx / z
	cmp.l	48(a1),d5		| z < zmin ?
	blt.s	8f
	move.l	d5,d6
	cmp.l	#32767,d6
	ble.s	3f
2:	asr.l	#1,d6			| scale down while z > 32767
	asr.l	#1,d3
	asr.l	#1,d4
	cmp.l	#32767,d6
	bgt.s	2b
3:	move.l	d3,d0
	divs.w	d6,d3
	bvs.s	5f
	cmp.w	#16000,d3		| keep room for + addx
	bgt.s	5f
	cmp.w	#-16000,d3
	blt.s	5f
	add.w	42(a1),d3		| + addx (low word)
	move.w	d3,12(a2)
	bra.s	6f
5:	| quotient overflow: far outside the screen
	move.w	#16000,d3
	tst.l	d0
	bpl.s	51f
	neg.w	d3
51:	move.w	d3,12(a2)
6:	move.l	d4,d0
	divs.w	d6,d4
	bvs.s	7f
	cmp.w	#16000,d4
	bgt.s	7f
	cmp.w	#-16000,d4
	blt.s	7f
	add.w	46(a1),d4		| + addy
	move.w	d4,14(a2)
	bra.s	8f
7:	move.w	#16000,d4
	tst.l	d0
	bpl.s	71f
	neg.w	d4
71:	move.w	d4,14(a2)
8:	dbf	d7,1b
9:	movem.l	(sp)+,d2-d7/a2-a6
	rts
