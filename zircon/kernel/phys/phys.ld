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
   * This layout is also used for fixed-position x86-32 boot shims,
   * which set PHYS_LOAD_ADDRESS via -defsym.
   */

   PHYS_LOAD_ADDRESS = DEFINED(PHYS_LOAD_ADDRESS) ? PHYS_LOAD_ADDRESS : 0;

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

  /*
   * TODO(mcgrathr): Clean things up so automatic orphans placement works
   * and this explicit handling is not required for every section.
   */
  BootCpuid : {
    PROVIDE_HIDDEN(__start_BootCpuid = .);
    *(BootCpuid)
    PROVIDE_HIDDEN(__stop_BootCpuid = .);
  }

  .data : {
    *(.data*)

    /*
     * For ZBI format, the file size must be aligned to 8 bytes.  Just aligning
     * doesn't do anything to the ultimate binary-format load image size when
     * there isn't any subsequent data.  So align and write a useless word.
     */
    . = ALIGN(8);
    QUAD(0xdeadd00dfeedface);
  }

  /*
   * This represents the end of the load image, as distinct from the end
   * of data in memory.  That is, it's not aligned to the .bss alignment.
   * That alignment padding isn't included in the file.  It only matters
   * that the memory reserved for .bss at runtime includes the padding.
   * But the load headers for boot headers need to identify the size of
   * the load image on disk precisely, so they use this instead of _edata.
   */
  PROVIDE_HIDDEN(PHYS_LOAD_END = .);

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
