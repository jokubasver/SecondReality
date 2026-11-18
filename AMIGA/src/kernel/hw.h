/* Amiga OCS/ECS hardware definitions */
#ifndef HW_H
#define HW_H

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

#define CUSTOM 0xdff000
#define REG16(o) (*(volatile u16 *)(CUSTOM + (o)))
#define REG32(o) (*(volatile u32 *)(CUSTOM + (o)))

#define BLTDDAT  REG16(0x000)
#define DMACONR  REG16(0x002)
#define VPOSR    REG16(0x004)
#define VHPOSR   REG16(0x006)
#define DSKDATR  REG16(0x008)
#define JOY0DAT  REG16(0x00a)
#define JOY1DAT  REG16(0x00c)
#define POTGOR   REG16(0x016)
#define DSKBYTR  REG16(0x01a)
#define INTENAR  REG16(0x01c)
#define INTREQR  REG16(0x01e)
#define DSKPT    REG32(0x020)
#define DSKLEN   REG16(0x024)
#define VPOSW    REG16(0x02a)
#define COPCON   REG16(0x02e)
#define SERPER   REG16(0x032)
#define POTGO    REG16(0x034)
#define BLTCON0  REG16(0x040)
#define BLTCON1  REG16(0x042)
#define BLTAFWM  REG16(0x044)
#define BLTALWM  REG16(0x046)
#define BLTCPT   REG32(0x048)
#define BLTBPT   REG32(0x04c)
#define BLTAPT   REG32(0x050)
#define BLTDPT   REG32(0x054)
#define BLTSIZE  REG16(0x058)
#define BLTCMOD  REG16(0x060)
#define BLTBMOD  REG16(0x062)
#define BLTAMOD  REG16(0x064)
#define BLTDMOD  REG16(0x066)
#define BLTCDAT  REG16(0x070)
#define BLTBDAT  REG16(0x072)
#define BLTADAT  REG16(0x074)
#define DSKSYNC  REG16(0x07e)
#define COP1LC   REG32(0x080)
#define COP2LC   REG32(0x084)
#define COPJMP1  REG16(0x088)
#define COPJMP2  REG16(0x08a)
#define DIWSTRT  REG16(0x08e)
#define DIWSTOP  REG16(0x090)
#define DDFSTRT  REG16(0x092)
#define DDFSTOP  REG16(0x094)
#define DMACON   REG16(0x096)
#define CLXCON   REG16(0x098)
#define INTENA   REG16(0x09a)
#define INTREQ   REG16(0x09c)
#define ADKCON   REG16(0x09e)
#define AUDLC(n)  REG32(0x0a0 + (n) * 16)
#define AUDLEN(n) REG16(0x0a4 + (n) * 16)
#define AUDPER(n) REG16(0x0a6 + (n) * 16)
#define AUDVOL(n) REG16(0x0a8 + (n) * 16)
#define AUDDAT(n) REG16(0x0aa + (n) * 16)
#define BPLPT(n) REG32(0x0e0 + (n) * 4)
#define BPLCON0  REG16(0x100)
#define BPLCON1  REG16(0x102)
#define BPLCON2  REG16(0x104)
#define BPLCON3  REG16(0x106)
#define BPL1MOD  REG16(0x108)
#define BPL2MOD  REG16(0x10a)
#define SPRPT(n) REG32(0x120 + (n) * 4)
#define SPRPOS(n) REG16(0x140 + (n) * 8)
#define SPRCTL(n) REG16(0x142 + (n) * 8)
#define SPRDATA(n) REG16(0x144 + (n) * 8)
#define SPRDATB(n) REG16(0x146 + (n) * 8)
#define COLOR(n) REG16(0x180 + (n) * 2)
#define BEAMCON0 REG16(0x1dc)

/* register offsets for copper lists */
#define R_BPL1PTH 0x0e0
#define R_BPLCON0 0x100
#define R_BPLCON1 0x102
#define R_BPLCON2 0x104
#define R_BPL1MOD 0x108
#define R_BPL2MOD 0x10a
#define R_DIWSTRT 0x08e
#define R_DIWSTOP 0x090
#define R_DDFSTRT 0x092
#define R_DDFSTOP 0x094
#define R_COLOR00 0x180
#define R_SPR0PTH 0x120
#define R_COPJMP2 0x08a
#define R_COP2LCH 0x084
#define R_INTREQ  0x09c
#define R_DMACON  0x096

/* DMACON bits */
#define DMAF_SETCLR 0x8000
#define DMAF_BLTDONE 0x4000
#define DMAF_BLTNZERO 0x2000
#define DMAF_BLITHOG 0x0400
#define DMAF_MASTER 0x0200
#define DMAF_RASTER 0x0100
#define DMAF_COPPER 0x0080
#define DMAF_BLITTER 0x0040
#define DMAF_SPRITE 0x0020
#define DMAF_DISK 0x0010
#define DMAF_AUD3 0x0008
#define DMAF_AUD2 0x0004
#define DMAF_AUD1 0x0002
#define DMAF_AUD0 0x0001
#define DMAF_ALL 0x01ff

/* INTENA/INTREQ bits */
#define INTF_SETCLR 0x8000
#define INTF_INTEN 0x4000
#define INTF_EXTER 0x2000
#define INTF_DSKSYNC 0x1000
#define INTF_RBF 0x0800
#define INTF_AUD3 0x0400
#define INTF_AUD2 0x0200
#define INTF_AUD1 0x0100
#define INTF_AUD0 0x0080
#define INTF_BLIT 0x0040
#define INTF_VERTB 0x0020
#define INTF_COPER 0x0010
#define INTF_PORTS 0x0008
#define INTF_SOFTINT 0x0004
#define INTF_DSKBLK 0x0002
#define INTF_TBE 0x0001

/* CIAs */
#define CIAA_PRA  (*(volatile u8 *)0xbfe001)
#define CIAA_PRB  (*(volatile u8 *)0xbfe101)
#define CIAA_DDRA (*(volatile u8 *)0xbfe201)
#define CIAA_DDRB (*(volatile u8 *)0xbfe301)
#define CIAA_TALO (*(volatile u8 *)0xbfe401)
#define CIAA_TAHI (*(volatile u8 *)0xbfe501)
#define CIAA_TBLO (*(volatile u8 *)0xbfe601)
#define CIAA_TBHI (*(volatile u8 *)0xbfe701)
#define CIAA_SDR  (*(volatile u8 *)0xbfec01)
#define CIAA_ICR  (*(volatile u8 *)0xbfed01)
#define CIAA_CRA  (*(volatile u8 *)0xbfee01)
#define CIAA_CRB  (*(volatile u8 *)0xbfef01)

#define CIAB_PRA  (*(volatile u8 *)0xbfd000)
#define CIAB_PRB  (*(volatile u8 *)0xbfd100)
#define CIAB_DDRA (*(volatile u8 *)0xbfd200)
#define CIAB_DDRB (*(volatile u8 *)0xbfd300)
#define CIAB_TALO (*(volatile u8 *)0xbfd400)
#define CIAB_TAHI (*(volatile u8 *)0xbfd500)
#define CIAB_TBLO (*(volatile u8 *)0xbfd600)
#define CIAB_TBHI (*(volatile u8 *)0xbfd700)
#define CIAB_ICR  (*(volatile u8 *)0xbfdd00)
#define CIAB_CRA  (*(volatile u8 *)0xbfde00)
#define CIAB_CRB  (*(volatile u8 *)0xbfdf00)

/* copper list helpers */
#define CMOVE(r, v) (((u32)(r) << 16) | ((v) & 0xffff))
#define CWAIT(y, x) (((u32)((y) & 0xff) << 24) | ((u32)(((x) & 0xfe) | 1) << 16) | 0xfffe)
#define CEND 0xfffffffe

#endif
