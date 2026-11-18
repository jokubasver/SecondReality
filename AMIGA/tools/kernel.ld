OUTPUT_FORMAT("elf32-m68k")
ENTRY(_start)
SECTIONS
{
  . = 0x400;
  .text : { _start = .; *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) . = ALIGN(4); }
  _image_end = .;
  .bss (NOLOAD) : { _bss_start = .; *(.bss .bss.*) *(COMMON) . = ALIGN(4); _bss_end = .; }
  ASSERT(_bss_end <= 0xA800, "kernel too large (stack collision)")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
