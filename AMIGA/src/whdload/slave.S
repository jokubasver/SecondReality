| WHDLoad slave for the Amiga port of Second Reality.
|
| Loads both disk images (disk.1, disk.2) into ExpMem, copies the kernel to
| its link address 0x400 and starts it at its WHDLoad entry (0x408) with
| a0 = disk images, a1 = resload, d0 = quit key.  The kernel then reads its
| files from memory instead of the floppy drive.  Fully PC relative.

	.equ	ADF_SIZE, 901120
	.equ	resload_Abort, 4
	.equ	resload_FlushCache, 32
	.equ	resload_DiskLoad, 40

	.text
base:
	moveq	#-1,d0			| ws_Security
	rts
	.ascii	"WHDLOADS"		| ws_ID
	.word	10			| ws_Version
	.word	3			| ws_Flags: WHDLF_Disk | WHDLF_NoError
	.long	0x100000		| ws_BaseMemSize: 1 MB chip
	.long	0			| ws_ExecInstall
	.word	start-base		| ws_GameLoader
	.word	0			| ws_CurrentDir
	.word	0			| ws_DontCache
	.byte	0			| ws_keydebug
keyexit:
	.byte	0x59			| ws_keyexit: F10
expmem:	.long	2*ADF_SIZE		| ws_ExpMem (address after install)
	.word	name-base		| ws_name
	.word	copy-base		| ws_copy
	.word	info-base		| ws_info

name:	.asciz	"Second Reality (Amiga OCS port)"
copy:	.asciz	"1993 Future Crew"
info:	.ascii	"Amiga port by Claude"
	.byte	10
	.ascii	"Quit with F10"
	.byte	0
	.even

start:
	move.l	a0,a2			| resload
	move.l	expmem(pc),a3
	moveq	#0,d0
	move.l	#ADF_SIZE,d1
	moveq	#1,d2
	move.l	a3,a0
	jsr	resload_DiskLoad(a2)
	moveq	#0,d0
	move.l	#ADF_SIZE,d1
	moveq	#2,d2
	move.l	a3,a0
	add.l	#ADF_SIZE,a0
	jsr	resload_DiskLoad(a2)
	| kernel image: disk 1 offset 1024 -> 0x400 (up to the stack area)
	lea	1024(a3),a0
	move.l	#0x400,a1
	move.w	#(0xa800-0x400)/4-1,d0
1:	move.l	(a0)+,(a1)+
	dbf	d0,1b
	jsr	resload_FlushCache(a2)
	move.l	a3,a0
	move.l	a2,a1
	moveq	#0,d0
	move.b	keyexit(pc),d0
	jmp	0x408
