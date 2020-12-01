/* Copyright 2020 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * This is essentially a "subroutine" of phys.ld, but has to go into a
 * separate file for BFD ld to handle it properly (lld works with this
 * just pasted onto the end of phys.ld).  For BFD ld, this file must be
 * listed in a -T switch *before* the -T switch for phys.ld so that the
 * `INSERT` clause can be "pending" when the main SECTIONS clause is read.
 *
 * Using a separate `SECTIONS ... INSERT` clause makes it possible to control
 * where orphan sections wind up relative to the magic symbols like _end and
 * PHYS_LOAD_END so that identifier-named sections can be used at will without
 * listing them all in explicitly in the linker script to get correct layout.
 */

SECTIONS {
  .zbi.align : ALIGN(8) {
    /*
     * For ZBI format, the file size must be aligned to 8 bytes.  Just aligning
     * doesn't do anything to the ultimate binary-format load image size when
     * there isn't any subsequent data.  So align and write a useless word.
     */
    QUAD(0xdeadd00dfeedface);

    /*
     * This represents the end of the load image, as distinct from the end
     * of data in memory.  That is, it's not aligned to the .bss alignment.
     * That alignment padding isn't included in the file.  It only matters
     * that the memory reserved for .bss at runtime includes the padding.
     * But the load headers for boot headers need to identify the size of
     * the load image on disk precisely, so they use this instead of _edata.
     */
    PROVIDE_HIDDEN(PHYS_LOAD_END = .);
  }
} INSERT BEFORE .bss
