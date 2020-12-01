/* Copyright 2020 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

SECTIONS {
  /*
   * A phys executable is purely position-independent so its link address
   * doesn't really matter to it--it only affects the debug file, not the
   * load image.  Using zero simplifies the arithmetic when debugging.
   *
   * It's all a single contiguous segment that is both writable and
   * executable, since no MMU protections are enabled.
   *
   * This layout is also used for fixed-position x86-32 boot shims.
   * The legacy x86-64 ZBI boot protocol is also fixed-position.
   * PHYS_LOAD_ADDRESS is always set via -defsym, either to the
   * fixed position or to 0 for the relocatable case.
   */
  . = PHYS_LOAD_ADDRESS;
  PROVIDE_HIDDEN(__code_start = .);

  .boot.header : {
    KEEP(*(.boot.header))
  } :load

  .text : {
    *(.text*)
  }

  .note.gnu.build-id : {
    PROVIDE_HIDDEN(__start_note_gnu_build_id = .);
    *(.note.gnu.build-id)
    PROVIDE_HIDDEN(__stop_note_gnu_build_id = .);
  } :load :note

  /*
   * Code compiled for kernels should generate .debug_frame, not .eh_frame.
   * We don't want to clutter the runtime image with this data, since it's
   * not used at runtime.  However, GCC currently generates .eh_frame even
   * given -fno-unwind-tables.  Also, the 32-bit x86 build can use outcalls
   * to libclang_rt.builtins (libgcc) functions, which will have been
   * compiled with .eh_frame generation.  So just discard all that data here.
   * Unfortunately this may let us overlook some regression where other code
   * is emitting .eh_frame rather than .debug_frame in the future.
   */
  /DISCARD/ : {
    *(.eh_frame)
  }

  .rodata : {
    *(.rodata*)
  } :load

  .data.rel.ro : {
    *(.data.rel.ro*)
  }
  ASSERT(SIZEOF(.data.rel.ro) == 0, "address constants in const initializers")

  .preinit_array : {
    KEEP(*(.preinit_array))
  }
  ASSERT(SIZEOF(.preinit_array) == 0, "no static constructors allowed")

  .init_array : {
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
    KEEP(*(SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.init_array))
  }
  ASSERT(SIZEOF(.init_array) == 0, "no static constructors allowed")

  .data : {
    *(.data*)
  }

  /*
   * Any writable orphan sections will be placed here.  The separate clause
   * `SECTIONS ... INSERT BEFORE .bss` in phys-end.ld will attach after the
   * orphans and before .bss.  Note that the linker invocation must use the
   * switches `-T phys-end.ld -T phys.ld`, *in that order* for BFD ld to work.
   */

  .bss : {
    /*
     * Despite the name, _edata really represents the start of bss rather than
     * the end of data.  Putting it here gives it the alignment of the output
     * .bss section so the start.S code can rely on its alignment.
     */
    PROVIDE_HIDDEN(_edata = .);

    *(.bss* .sbss* COMMON)

    /*
     * The start.S code relies on the alignment of the end of .bss as well.
     * This implicitly ensures the alignment of the start of the section
     * is at least this much.
     */
    . = ALIGN(16);
    PROVIDE_HIDDEN(_end = .);
  }

  /*
   * This is the amount of memory past the load image that should be
   * reserved by the boot loader.  In a ZBI executable, it goes into
   * the zbi_kernel_t::reserve_memory_size field.
   */
  PROVIDE_HIDDEN(PHYS_RESERVE_MEMORY_SIZE = _end - PHYS_LOAD_END);
}

PHDRS {
  load PT_LOAD FLAGS(7);  /* PF_R|PF_W|PF_X */
  note PT_NOTE FLAGS(4);  /* PF_R */
}

/*
 * This has no real effect since the ELF headers aren't used at runtime.
 * But it makes e_entry in the debug file match the entry point address.
 */
ENTRY(_start)
