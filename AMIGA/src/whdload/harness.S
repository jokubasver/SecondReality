| Test harness that stands in for WHDLoad in the emulator (not shipped).
| Booted from DF0: reads DF1/DF2 (the two demo ADFs) into fast memory, kills
| the OS and calls the slave (appended at "slave") with a fake resload table.
| Green screen = resload_Abort reached, red = unsupported resload call.
	.equ	ADF_SIZE, 901120
	.text
hstart:
	move.l	a1,a5
	move.l	#2*ADF_SIZE,d0
	moveq	#4,d1			| MEMF_FAST
	jsr	-198(a6)
	tst.l	d0
	beq	fail_1
	bra.s	ok_1
fail_1:	move.w	#0x00f,0xdff180
	bra.s	fail_1
ok_1:
	lea	images(pc),a0
	move.l	d0,(a0)
	move.l	#2*ADF_SIZE,d0
	moveq	#4,d1
	jsr	-198(a6)
	tst.l	d0
	beq	fail_2
	bra.s	ok_2
fail_2:	move.w	#0x00f,0xdff180
	bra.s	fail_2
ok_2:
	lea	slave(pc),a0
	move.l	d0,32(a0)		| ws_ExpMem
	move.l	#8192,d0
	moveq	#4,d1
	jsr	-198(a6)
	tst.l	d0
	beq	fail_3
	bra.s	ok_3
fail_3:	move.w	#0x00f,0xdff180
	bra.s	fail_3
ok_3:
	add.l	#8192,d0
	lea	stack(pc),a0
	move.l	d0,(a0)
	| motor off on DF0
	move.l	a5,a1
	move.w	#9,28(a1)
	clr.l	36(a1)
	jsr	-456(a6)
	moveq	#1,d7
unit:	move.l	#64,d0
	move.l	#0x10001,d1
	jsr	-198(a6)
	tst.l	d0
	beq	fail_4
	bra.s	ok_4
fail_4:	move.w	#0x008,0xdff180
	bra.s	fail_4
ok_4:
	move.l	d0,a3
	move.l	a5,a0
	move.l	a3,a1
	moveq	#47,d0
1:	move.b	(a0)+,(a1)+
	dbf	d0,1b
	lea	tdname(pc),a0
	move.l	d7,d0
	move.l	a3,a1
	moveq	#0,d1
	jsr	-444(a6)		| OpenDevice
	tst.l	d0
	bne	fail_5
	bra.s	ok_5
fail_5:	move.w	#0xff0,0xdff180
	bra.s	fail_5
ok_5:
	move.l	a3,a1
	move.w	#2,28(a1)
	move.l	images(pc),d0
	cmp.w	#1,d7
	beq.s	2f
	add.l	#ADF_SIZE,d0
2:	move.l	d0,40(a1)
	move.l	#ADF_SIZE,36(a1)
	clr.l	44(a1)
	jsr	-456(a6)
	tst.b	31(a3)
	bne	fail_6
	bra.s	ok_6
fail_6:	move.w	#0xf0f,0xdff180
	bra.s	fail_6
ok_6:
	move.l	a3,a1
	move.w	#9,28(a1)
	clr.l	36(a1)
	jsr	-456(a6)
	addq.w	#1,d7
	cmp.w	#3,d7
	bne	unit
	lea	super(pc),a5
	jsr	-30(a6)
super:	move.w	#0x2700,sr
	move.l	stack(pc),sp
	lea	0xdff000,a0
	move.w	#0x7fff,0x9a(a0)
	move.w	#0x7fff,0x9c(a0)
	move.w	#0x7fff,0x96(a0)
	move.b	#0x7f,0xbfed01
	move.b	#0x7f,0xbfdd00
	lea	resload(pc),a0
	lea	slave(pc),a1
	moveq	#0,d0
	move.w	24(a1),d0
	add.l	d0,a1
	jmp	(a1)

resload:
	bra.w	r_bad			| 0 Install
	bra.w	r_abort			| 4 Abort
	.rept	6
	bra.w	r_bad			| 8..28
	.endr
	bra.w	r_flush			| 32 FlushCache
	bra.w	r_bad			| 36 GetFileSize
	bra.w	r_disk			| 40 DiskLoad
	.rept	8
	bra.w	r_bad
	.endr

r_rts:	rts
| 68020/030 only (what the emulator test uses)
r_flush:
	move.l	d0,-(sp)
	.word	0x4e7a,0x0002		| movec cacr,d0
	or.w	#0x0808,d0
	.word	0x4e7b,0x0002		| movec d0,cacr
	move.l	(sp)+,d0
	rts
r_disk:	movem.l	d1-d2/a1,-(sp)
	move.l	images(pc),a1
	cmp.l	#1,d2
	beq.s	1f
	add.l	#ADF_SIZE,a1
1:	add.l	d0,a1
	lsr.l	#2,d1
	bra.s	3f
2:	move.l	(a1)+,(a0)+
3:	subq.l	#1,d1
	bpl.s	2b
	movem.l	(sp)+,d1-d2/a1
	moveq	#-1,d0
	rts
r_abort:
	move.w	#0x2700,sr
	move.w	#0x7fff,0xdff096
1:	move.w	#0x0f0,0xdff180
	bra.s	1b
r_bad:
fail:	move.w	#0x0ff,0xdff180
	bra.s	fail

images:	.long	0
stack:	.long	0
tdname:	.asciz	"trackdisk.device"
	.balign	4
slave:
