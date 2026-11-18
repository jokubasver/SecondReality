OUTPUT_FORMAT("elf32-m68k")
ENTRY(part_main)
SECTIONS
{
  . = 0;
  .text : { *(.text.entry) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) . = ALIGN(4); }
  _image_end = .;
  .bss (NOLOAD) : { _bss_start = .; *(.bss .bss.*) *(COMMON) . = ALIGN(4); _bss_end = .; }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
