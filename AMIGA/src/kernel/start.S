| Kernel entry, relocation and interrupt entry points.
|
| The bootblock loads the kernel image into memory allocated by exec and
| jumps to _start in user mode with a6 = ExecBase.  We enter supervisor mode,
| kill the OS, move the image to its link address (0x400) and start kmain.

	.equ	SSP_TOP, 0xC000

	.section .text.start,"ax"
	.globl	_start
_start:
	bra.w	os_start
	.ascii	"WHDK"
| WHDLoad entry (0x408): the slave has copied the image to its link address
| and calls us in supervisor mode with a0 = disk images (ExpMem), a1 =
| resload base, d0 = quit key.
whd_entry:
	move.w	#0x2700,sr
	move.l	a0,k_ramdisk
	move.l	a1,k_whd_resload
	move.b	d0,k_whd_quitkey
	lea	0xdff000,a0
	move.w	#0x7fff,0x9a(a0)
	move.w	#0x7fff,0x9c(a0)
	move.w	#0x7fff,0x96(a0)
	move.w	#0x7f00,0x9e(a0)
	move.b	#0x7f,0xbfed01
	move.b	#0x7f,0xbfdd00
	moveq	#0,d7			| WHDLoad manages the caches
	jmp	kentry
os_start:
	lea	super(pc),a5
	jsr	-30(a6)			| exec Supervisor()
super:
	move.w	#0x2700,sr
	move.w	0x128(a6),d7		| exec AttnFlags: CPU type, kept for kentry
	lea	0xdff000,a0
	move.w	#0x7fff,0x9a(a0)	| INTENA: all off
	move.w	#0x7fff,0x9c(a0)	| INTREQ: clear
	move.w	#0x7fff,0x9c(a0)
	move.w	#0x7fff,0x96(a0)	| DMACON: all off
	move.w	#0x7f00,0x9e(a0)	| ADKCON
	move.b	#0x7f,0xbfed01		| CIA-A ICR off
	move.b	#0x7f,0xbfdd00		| CIA-B ICR off
	move.w	#0x000,0x180(a0)
	| copy the relocator to 0x100 (unused user vectors)
	lea	reloc_code(pc),a0
	lea	0x100,a1
	move.w	#(reloc_end-reloc_code)/2-1,d0
1:	move.w	(a0)+,(a1)+
	dbf	d0,1b
	lea	_start(pc),a0
	lea	_start,a1
	move.l	#_image_end,d0
	sub.l	#_start,d0
	addq.l	#3,d0
	lsr.l	#2,d0			| longwords
	jmp	0x100

reloc_code:
	cmp.l	a0,a1
	beq.s	3f
	bhi.s	2f
	subq.l	#1,d0
1:	move.l	(a0)+,(a1)+
	dbf	d0,1b
	bra.s	3f
2:	move.l	d0,d1
	lsl.l	#2,d1
	add.l	d1,a0
	add.l	d1,a1
	subq.l	#1,d0
4:	move.l	-(a0),-(a1)
	dbf	d0,4b
3:	jmp	kentry
reloc_end:

	.text
	.globl	kentry
kentry:
	lea	SSP_TOP,sp
	| clear bss
	lea	_bss_start,a0
	lea	_bss_end,a1
1:	cmp.l	a1,a0
	bhs.s	2f
	clr.w	(a0)+
	bra.s	1b
2:
	move.w	d7,k_cpu_flags
	| 68020+: instruction cache only (the blitter and disk DMA write chip
	| memory behind a data cache's back)
	btst	#1,d7
	beq.s	6f
	btst	#3,d7
	bne.s	7f
	move.l	#0x0809,d0		| 020/030: clear + enable I-cache, D-cache off
	.word	0x4e7b,0x0002		| movec d0,cacr
	bra.s	6f
7:	.word	0xf4f8			| 040/060: cpusha bc
	move.l	#0x8000,d0		| I-cache on, D-cache off
	.word	0x4e7b,0x0002		| movec d0,cacr
6:
	| install exception vectors
	lea	exc_vec_table,a0
3:	move.l	(a0)+,d0
	beq.s	5f
	lsl.l	#2,d0
	move.l	d0,a1
	move.l	(a0)+,(a1)
	bra.s	3b
5:
	move.l	#lev_dummy,0x60		| spurious
	move.l	#lev1,0x64
	move.l	#lev2,0x68
	move.l	#lev3,0x6c
	move.l	#lev4,0x70
	move.l	#lev5,0x74
	move.l	#lev6,0x78
	move.l	#lev_dummy,0x7c
	jsr	kmain
	| kmain never returns
4:	bra.s	4b

| ---------------------------------------------------------------------------
| Exception handlers: one stub per vector, then a register dump screen.
	.irp v,2,3,4,5,6,7,8,9,10,11,12,13,14,15,24,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
exc\v:	move.w	#0x2700,sr
	move.w	#\v,exc_vec
	bra	exc_common
	.endr
exc_common:
	movem.l	d0-d7/a0-a7,exc_regs
	move.l	sp,exc_frame
	lea	0x7000,sp
	move.l	exc_frame,-(sp)
	pea	exc_regs
	moveq	#0,d0
	move.w	exc_vec,d0
	move.l	d0,-(sp)
	jsr	k_panic
1:	bra.s	1b
	.globl	exc_vec_table
exc_vec_table:
	.irp v,2,3,4,5,6,7,8,9,10,11,12,13,14,15,24,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
	.long	\v, exc\v
	.endr
	.long	0, 0
	.bss
exc_vec: .skip 2
	.even
exc_regs: .skip 64
exc_frame: .skip 4
	.text

| ---------------------------------------------------------------------------
| Interrupt entry points.  C handlers are called with the scratch registers
| saved; they acknowledge INTREQ themselves.
lev1:
	move.w	#0x0007,0xdff09c
	move.w	#0x0007,0xdff09c
	rte
lev2:
	movem.l	d0-d1/a0-a1,-(sp)
	jsr	k_level2
	movem.l	(sp)+,d0-d1/a0-a1
	rte
lev3:
	movem.l	d0-d1/a0-a1,-(sp)
	jsr	k_level3
	movem.l	(sp)+,d0-d1/a0-a1
	rte
lev4:
	move.w	#0x0780,0xdff09c
	move.w	#0x0780,0xdff09c
	rte
lev5:
	move.w	#0x1800,0xdff09c
	move.w	#0x1800,0xdff09c
	rte
lev6:
	movem.l	d0-d1/a0-a1,-(sp)
	lea	16(sp),a0
	move.l	a0,k_lev6_frame
	jsr	k_level6
	movem.l	(sp)+,d0-d1/a0-a1
	rte
lev_dummy:
	rte

| ---------------------------------------------------------------------------
| void k_set_ipl(int level) -- sets the CPU interrupt mask
	.globl	k_set_ipl
k_set_ipl:
	move.l	4(sp),d0
	and.w	#7,d0
	lsl.w	#8,d0
	or.w	#0x2000,d0
	move.w	d0,sr
	rts

| void k_whd_quit(void) -- returns to WHDLoad (resload_Abort, TDREASON_OK)
	.globl	k_whd_quit
k_whd_quit:
	move.l	k_whd_resload,a0
	pea	-1.w
	jmp	4(a0)

| void k_flush_cache(void) -- after code was written (part relocation,
| patched code): needed on 68020+ and on JIT emulators such as Emu68
	.globl	k_flush_cache
k_flush_cache:
	move.l	k_whd_resload,d0
	beq.s	1f
	move.l	d0,a0
	jmp	32(a0)			| resload_FlushCache
1:	move.w	k_cpu_flags,d0
	btst	#1,d0
	beq.s	3f			| 68000/010: no caches
	btst	#3,d0
	bne.s	2f
	.word	0x4e7a,0x0002		| movec cacr,d0
	or.w	#0x0808,d0		| clear instruction and data caches
	.word	0x4e7b,0x0002		| movec d0,cacr
	rts
2:	.word	0xf4f8			| cpusha bc
3:	rts

	.bss
	.globl	k_cpu_flags
	.even
k_cpu_flags: .skip 2
	.text

| int k_get_sr(void)
	.globl	k_get_sr
k_get_sr:
	moveq	#0,d0
	move.w	sr,d0
	rts
