/* Copyright 2020 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * This provides the zbi_kernel_t (see <zircon/boot/image.h>) payload header
 * for ZBI executables, inserted immediately after the ZBI container and item
 * headers zbi-header.S generates.  For BFD ld, this file must be listed in a
 * -T switch *before* the -T switch for phys.ld so that the `INSERT` clause
 * can be "pending" when the main SECTIONS clause is read.
 */

SECTIONS {
  .boot.header.zbi_kernel_t : ALIGN(8) {
    /*
     * Two words: the entry point address/offset, and the bss size.
     * In the relocatable protocol, PHYS_LOAD_ADDRESS is zero and
     * this is taken as an offset from the actual load address.
     * In the legacy fixed-address protocol, it's an exact address.
     */
    QUAD(_start - __code_start + PHYS_LOAD_ADDRESS);
    QUAD(PHYS_RESERVE_MEMORY_SIZE);
  }
} INSERT AFTER .boot.header
