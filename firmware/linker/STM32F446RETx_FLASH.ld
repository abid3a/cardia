/* STM32F446RETx_FLASH.ld -- link Cardia into internal flash.
 *
 * STM32F446RE: 512 KB flash at 0x08000000, 128 KB SRAM at 0x20000000
 * (SRAM1 112 KB + SRAM2 16 KB, contiguous, so they are declared as one region).
 *
 * Two things in here are load-bearing and worth stating explicitly, because
 * getting either wrong produces a board that runs but misbehaves:
 *
 * 1. HEAP SIZE IS ZERO, ON PURPOSE.
 *    Nothing in this firmware calls malloc. Every buffer -- the DMA ping-pong
 *    pair, the pipeline state, the network's activation scratch -- is a static
 *    object whose size is fixed at link time. That is a deliberate design rule
 *    for a device that is supposed to keep classifying beats indefinitely: a
 *    heap introduces an allocation that can fail, a fragmentation mode that
 *    only shows up after hours of uptime, and a memory figure that is a
 *    probability distribution rather than a number. With _Min_Heap_Size = 0,
 *    the link fails loudly if anything ever pulls in an allocator that needs
 *    room, instead of quietly working on the bench and failing in the field.
 *    (firmware/src/syscalls.c backs this up at runtime: _sbrk traps.)
 *
 * 2. .data HAS TWO ADDRESSES AND STARTUP MUST BRIDGE THEM.
 *    Initialised variables must be *stored* in flash (they have to survive
 *    power-off) but *live* in RAM (they have to be writable). The AT> keyword
 *    below gives .data a load address in flash (_sidata) and a virtual address
 *    in RAM (_sdata.._edata). No hardware performs that copy. Reset_Handler in
 *    startup_stm32f446xx.c does it, before main, and if that loop is removed
 *    every initialised global reads as garbage.
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}

/* The stack grows downwards from the top of RAM. */
_estack = ORIGIN(RAM) + LENGTH(RAM);

_Min_Heap_Size  = 0x0;      /* see note 1 above -- no dynamic allocation */
_Min_Stack_Size = 0x1000;   /* 4 KB: deepest path is main -> pipeline_step ->
                               cardia_classify, all of whose big buffers are
                               static, plus one interrupt frame with FPU state */

/* Program headers, declared explicitly.
 *
 * Without this block ld invents its own segments, and because .data's LOAD
 * address sits immediately after .rodata in flash it merges the read-execute
 * code and the read-write data into a single PT_LOAD -- which is then marked
 * RWX, and binutils 2.39+ warns about it. The warning is worth taking
 * seriously rather than silencing with --no-warn-rwx-segments: a segment that
 * is simultaneously writable and executable is exactly what an MPU
 * configuration or a secure-boot loader is supposed to reject, and describing
 * the image accurately now costs six lines.
 *
 * FLAGS are the ELF p_flags bits: 4 = R, 2 = W, 1 = X.
 */
PHDRS
{
  text PT_LOAD FLAGS(5);   /* R + X -- code and constants, executed from flash */
  data PT_LOAD FLAGS(6);   /* R + W -- initialised data and zeroed RAM         */
}

SECTIONS
{
  /* The vector table must be the first thing in flash: after reset the core
     fetches the initial SP from 0x08000000 and the initial PC from 0x08000004
     before executing a single instruction. */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH :text

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH :text

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH :text

  /* Unwind tables. Nothing here throws or unwinds, but the linker emits
     references to __exidx_start/__exidx_end and libgcc expects the section to
     exist, so it is kept rather than discarded. */
  .ARM.extab : {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH :text

  .ARM : {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH :text

  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH :text

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH :text

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH :text

  /* Load address of .data in flash. Reset_Handler copies from here. */
  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH :data

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM :data

  /* Not a real section so much as a link-time assertion: reserve the stack (and
     the nominally-zero heap) and let the linker fail if what remains of RAM
     cannot hold them. That turns "we ran out of stack" from a 3 a.m. hard fault
     into a build error.

     (NOLOAD) because there are no contents to load -- this only advances the
     location counter -- and :NONE because it belongs to no program header at
     all. Left in the data segment, the linker tries to give it a flash load
     address, discovers it has to slide that address past the whole of .bss to
     keep the segment's LMA-VMA offset consistent, and warns about the
     adjustment. The reservation is a RAM budget assertion; it is not part of
     the loadable image, so it should not be described as though it were. */
  ._user_heap_stack (NOLOAD) :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    PROVIDE(__end__ = .);
    PROVIDE(__heap_start = .);
    . = . + _Min_Heap_Size;
    PROVIDE(__heap_end = .);
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM :NONE

  /* Newlib's crt and nosys stubs reference these names; provide them so the
     link does not depend on which spec files a caller passes. */
  PROVIDE(__StackTop   = _estack);
  PROVIDE(__StackLimit = _estack - _Min_Stack_Size);
  PROVIDE(__stack      = _estack);

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
