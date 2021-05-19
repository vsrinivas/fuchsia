/* Copyright 2021 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

/*
 * This file is included as an input linker script to support the assembly code
 * in linuxboot-header.S when used in a phys_executable() target.  It defines
 * the link-time base address needed by phys.ld and also defines values for
 * linuxboot-header.S to use that cannot be represented directly there.
 */

/*
 * The link-time address is always 0 but the image can be loaded anywhere in
 * memory.  The Linux protocol guarantees 2MiB alignment, but our phys
 * executables only ever assume the ZBI protocol's 64KiB alignment (or less).
 */
HIDDEN(PHYS_LOAD_ADDRESS = 0);

/*
 * Size of the kernel in memory, including the bss after the image so that the
 * boot loader is sure to place the kernel where it has enough headroom after
 * the storage image is loaded.  This symbol is used by the linuxboot-header.S
 * assembly code to embed this value, which cannot be calculated directly by a
 * single relocation record without triggering non-pure-PIC sanity checks.
 */
HIDDEN(LINUXBOOT_SIZE = _end - PHYS_LOAD_ADDRESS);
