/* Copyright 2022 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is a linker script for producing a flat binary file containing
 * the contents of pure read-only data (in C and C++, const variables
 * not containing pointers).  The data is anchored at a symbol (global
 * variable) called `kRodata`.  The initializer for this variable should
 * be the only thing (other than declarations) in the compiled sources.
 */

SECTIONS {
  . = 0;

  /*
   * Preserve only the special section.
   */
  .rodata : { *(.rodata-extract) }

  /*
   * Discard any other allocated sections.  There shouldn't really be any,
   * but this may cover any uninlined static functions from headers that
   * don't get optimized out.
   */
  /DISCARD/ : {
    INPUT_SECTION_FLAGS(SHF_ALLOC) *(*)
  }
}

/*
 * The entry point in the ELF header is never really used, but this keeps
 * `kRodata` as the sole root protected from link-time garbage collection.
 */
ENTRY(kRodata)
