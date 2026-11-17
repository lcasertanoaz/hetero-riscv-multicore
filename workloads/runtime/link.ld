/* Linker script for the hetero-riscv-multicore harness.
 * Based on riscv-tests benchmarks/common/test.ld. Adds _bss_start/_bss_end
 * (zeroed by hart 0 in crt.S) and a NOLOAD .noinit section for the large
 * per-hart task scratch buffers, so they are neither loaded nor zeroed. */

OUTPUT_ARCH( "riscv" )
ENTRY(_start)

SECTIONS
{
  . = 0x80000000;
  .text.init : { *(.text.init) }

  . = ALIGN(0x1000);
  .tohost : { *(.tohost) }

  . = ALIGN(0x1000);
  .text : { *(.text) *(.text.*) }

  .rodata : { *(.rodata) *(.rodata.*) }

  .data : { *(.data) *(.data.*) }

  .sdata : {
    __global_pointer$ = . + 0x800;
    *(.srodata.cst16) *(.srodata.cst8) *(.srodata.cst4) *(.srodata.cst2) *(.srodata*)
    *(.sdata .sdata.* .gnu.linkonce.s.*)
  }

  . = ALIGN(8);
  _bss_start = .;
  .sbss : {
    *(.sbss .sbss.* .gnu.linkonce.sb.*)
    *(.scommon)
  }
  .bss : { *(.bss) *(.bss.*) *(COMMON) }
  . = ALIGN(8);
  _bss_end = .;

  .tdata :
  {
    _tdata_begin = .;
    *(.tdata)
    _tdata_end = .;
  }
  .tbss :
  {
    *(.tbss)
    _tbss_end = .;
  }

  /* Per-hart scratch for task data: not in the ELF, not zeroed. */
  . = ALIGN(64);
  .noinit (NOLOAD) : { *(.noinit) }

  /* Per-hart stacks + TLS start here (see crt.S) */
  _end = .;
}
