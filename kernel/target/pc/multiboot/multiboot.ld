/* Copyright 2018 The Fuchsia Authors
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)

ENTRY(_start)
SECTIONS
{
    PHYS_LOAD_ADDRESS = 0x100000;
    . = PHYS_LOAD_ADDRESS + SIZEOF_HEADERS;

    /*
     * The boot loader only loads a single contiguous segment.
     */
    .multiboot : {
        /*
         * The Multiboot header goes first.  The Multiboot format
         * requires that it be somewhere in the first 8192 bytes.
         */
        KEEP(*(.multiboot.header))
        ASSERT(ABSOLUTE(.) <= PHYS_LOAD_ADDRESS + 8192, "headers too big");

        /*
         * Next come the code, the RODATA, and the writable data.
         * They're all actually intermingled in one big segment
         * that is both writable and executable.  But we segregate
         * them slightly just for cache optimization.
         */
        *(.text*)
        *(.note.gnu.build-id* .rodata*)
        *(.data*)

        . = ALIGN(8);
        PROVIDE_HIDDEN(edata = .);
    }

    /*
     * Finally, the bss.
     */
    .bss : {
        *(.bss*) *(.sbss*) *(COMMON)
        . = ALIGN(8);
    }

    PROVIDE_HIDDEN(end = .);
}
