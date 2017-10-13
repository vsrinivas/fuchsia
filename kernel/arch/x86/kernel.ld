/*
// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2013 Travis Geiselbrecht
// Copyright (c) 2015 Intel Corporation
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
*/


ENTRY(_multiboot_start)
SECTIONS
{
    . = %KERNEL_BASE% + %KERNEL_LOAD_OFFSET%;
    __code_start = .;

    .text.boot0 : AT(%MEMBASE% + %KERNEL_LOAD_OFFSET%) {
        KEEP(*(.text.boot0))
    } =0x9090

    . = ALIGN(8);
    .buildsig : {
        buildsig = .;
        BYTE(0x42); BYTE(0x53); BYTE(0x49); BYTE(0x47); /* BSIG */
        BYTE(0x53); BYTE(0x54); BYTE(0x52); BYTE(0x54); /* STRT */
        /*
         * The self-pointer gives a local basis to compute the relative
         * positions of the lk_version_t and .note.gnu.build-id pointers
         * without already knowing the kernel's virtual address base.
         */
        QUAD(buildsig);
        QUAD(version);
        QUAD(__build_id_note_start);
        BYTE(0x42); BYTE(0x53); BYTE(0x49); BYTE(0x47); /* BSIG */
        BYTE(0x45); BYTE(0x4e); BYTE(0x44); BYTE(0x53); /* ENDS */
     }

    .text.boot : {
        KEEP(*(.text.boot))
    }

    .text : {
        *(.text* .sram.text)
        *(.gnu.linkonce.t.*)
    } =0x9090

    __code_end = .;

    . = ALIGN(4096);
    __rodata_start = .;

    .note.gnu.build-id : {
        __build_id_note_start = .;
        *(.note.gnu.build-id)
        __build_id_note_end = .;
    }

    .rodata : {
        *(.rodata* .gnu.linkonce.r.*)
    }

    .init_array : ALIGN(8) {
        __init_array_start = .;
        KEEP(*(.init_array .ctors))
        __init_array_end = .;
    }

    /*
     * Any read-only data "orphan" sections will be inserted here.
     * Ideally we'd put those into the .rodata output section, but
     * there isn't a way to do that that guarantees all same-named
     * input sections collect together as a contiguous unit, which
     * is what we need them for.  Linkers differ in how they'll
     * place another dummy section here relative to the orphans, so
     * there's no good way to define __rodata_end to be exactly the
     * end of all the orphans sections.  But the only use we have
     * for __rodata_end is to round it up to page size anyway, so
     * just define it inside the .data section below, which is
     * exactly the end of the orphans rounded up to the next page.
     */

    .data : ALIGN(4096) {
        __rodata_end = .;
        __data_start = .;
        *(.data .data.* .gnu.linkonce.d.*)
        __data_end = .;
    }

    /*
     * Any writable orphan sections would be inserted here.
     * But there's no way to put the __data_end symbol after
     * them, so we cannot allow any such cases.  There is no
     * good way to assert that, though.
     */

    .bss : ALIGN(4096) {
        __bss_start = .;
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    . = ALIGN(4096);
    _end = .;

    /* put a symbol arbitrarily 4MB past the end of the kernel */
    /* used by the heap and other early boot time allocators */
    _end_of_ram = . + (4 * 1024 * 1024);

    /DISCARD/ : { *(.comment) }
}
