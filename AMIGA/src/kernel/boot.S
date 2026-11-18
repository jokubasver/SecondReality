| Bootblock: loads the kernel (KSIZE bytes from disk offset 1024) into chip
| memory via trackdisk.device and jumps to it.  a1 = IORequest, a6 = ExecBase.
	.text
boot:
	move.l	a1,a5
	move.l	#KSIZE,d0
	moveq	#2,d1			| MEMF_CHIP
	jsr	-198(a6)		| AllocMem
	tst.l	d0
	beq.s	fail
	move.l	d0,a4
	move.l	a5,a1
	move.w	#2,28(a1)		| CMD_READ
	move.l	a4,40(a1)		| io_Data
	move.l	#KSIZE,36(a1)		| io_Length
	move.l	#1024,44(a1)		| io_Offset
	jsr	-456(a6)		| DoIO
	tst.b	31(a5)			| io_Error
	bne.s	fail
	move.l	a5,a1
	move.w	#9,28(a1)		| TD_MOTOR off
	clr.l	36(a1)
	jsr	-456(a6)
	jmp	(a4)
fail:	move.w	#0xf00,0xdff180
	bra.s	fail
