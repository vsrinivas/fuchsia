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

    .multiboot.header : {
        /*
         * The Multiboot header goes next.  The Multiboot format
         * requires that it be somewhere in the first 8192 bytes.
         */
        KEEP(*(.multiboot.header))
        ASSERT(ABSOLUTE(.) <= PHYS_LOAD_ADDRESS + 8192, "headers too big");
    } :load

    .note.gnu.build-id : {
        *(.note.gnu.build-id)
    } :load :note

    .rodata : {
        *(.rodata*)
    } :load

    .text : {
        *(.text*)
    } :load

    .data : {
        *(.data*)
        PROVIDE_HIDDEN(edata = .);
    } :load

    .bss : {
        *(.bss*) *(.sbss*) *(COMMON)
    } :load

    PROVIDE_HIDDEN(end = .);
}

PHDRS
{
    load PT_LOAD FLAGS(7); /* PF_R|PF_W|PF_X */
    note PT_NOTE FLAGS(4); /* PF_R */
}
