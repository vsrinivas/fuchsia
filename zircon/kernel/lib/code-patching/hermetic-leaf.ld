/*
 * Copyright 2021 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * This sets the entry point in the ELF file and avoids a warning about not
 * setting an explicit entry point, but it has no effect.  The assertion below
 * ensures that the entry point is always at the beginning of the blob.
 */
ENTRY(HERMETIC_LEAF_ENTRY)

/*
 * This is a linker script to produce a single hermetic leaf function binary blob.
 * It's used with --oformat binary to directly produce the raw blob to be patched in.
 * It requires the global symbol HERMETIC_LEAF_ENTRY be set to the function's entry point,
 * and requires that this entry point be the first instruction in the code.
 * No non-code sections are allowed.
 */
SECTIONS {
  . = 0;

  /*
   * The .text* sections become the actual binary blob.  The entry point must be first.
   */
  .text : { *(.text*) }
  ASSERT(HERMETIC_LEAF_ENTRY == 0, "Hermetic leaf functions require that the entry point is at the beginning of the generated code.")

  /*
   * These clauses detect any relocation records that haven't been applied by the hermetic link.
   * No references outside the hermetic blob are allowed.
   */
  .relr : { *(.relr.*) }
  .rela : { *(.rela.*) }
  .rel : { *(.rel.*) }
  ASSERT(SIZEOF(.relr) + SIZEOF(.rela) + SIZEOF(.rel) == 0, "Hermetic leaf function requires dynamic relocations, which are not supported.")

  /*
   * This should detect any section not called .text*, such as .data et al.
   * These are not allowed.
   */
  .other : { INPUT_SECTION_FLAGS(SHF_ALLOC) *(*) }
  ASSERT(SIZEOF(.other) == 0, "unexpected sections other than .text*")
}
