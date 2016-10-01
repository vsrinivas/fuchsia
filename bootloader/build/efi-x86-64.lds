/* Copyright 2016 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)
SECTIONS
{
    . = 0x2000;
    . = ALIGN(4096);
    .text :
    {
       *(.text)
       *(.text.*)
       . = ALIGN(16);
    }

    . = ALIGN(4096);
    .data :
    {
        *(.rodata*)
        *(.got.plt)
        *(.got)
        *(.data*)
        *(.sdata)
        /* TODO: figure out how to move .bss in such a way that the PE32+ loader
         * doesn't complain.
         */
        *(.sbss)
        *(.scommon)
        *(.dynbss)
        *(.bss)
        *(COMMON)
        *(.rel.local)
    }

    . = ALIGN(4096);

    /* A .reloc section is required to flag the final PE32+ file as relocatable.
     * Here we hardcode one block with one entry to keep the PE32+ loader happy.
     */
    _a = .;
    . = ALIGN(4096);
    .reloc :
    {
        LONG(. - _a);
        LONG(10);
        SHORT(0);
    }
}
