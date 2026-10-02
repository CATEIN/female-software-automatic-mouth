MEMORY { FLASH (rx) : ORIGIN = 0x10000000, LENGTH = 2M  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 264K }
ENTRY(_start)
SECTIONS
{
  .text : { *(.text._start) *crt0.o(.text) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  __data_load = LOADADDR(.data);
  .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT > FLASH
  .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; end = .; } > RAM
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
