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

ENTRY(_start)
SECTIONS
{
    . = %KERNEL_BASE% + %KERNEL_LOAD_OFFSET%;
    __code_start = .;

    .text.boot0 : AT(%MEMBASE% + %KERNEL_LOAD_OFFSET%) {
        KEEP(*(.text.boot0))
    } :code

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
        *(.text.boot)
    }

    .text : {
        *(.text* .sram.text)
        *(.gnu.linkonce.t.*)
    }

    __code_end = .;

    . = ALIGN(4096);
    __rodata_start = .;

    /*
     * These are page-aligned, so place them first.
     */
    .rodata.rodso_image : {
        *(.rodata.rodso_image.*)
    }

    .note.gnu.build-id : {
        __build_id_note_start = .;
        *(.note.gnu.build-id)
        __build_id_note_end = .;
    } :rodata :note

    .rodata : {
        *(.rodata* .gnu.linkonce.r.*)
    } :rodata

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
    } :data

    /*
     * Any writable orphan sections would be inserted here.
     * But there's no way to put the __data_end symbol after
     * them, so we cannot allow any such cases.  There is no
     * good way to assert that, though.
     */

    .bss : ALIGN(4096) {
        __bss_start = .;
        KEEP(*(.bss.prebss.*))
        . = ALIGN(16);
        __post_prebss_bss_start = .;
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
    }

    /*
     * Any SHT_NOBITS (.bss-like) sections would be inserted here.
     */

     . = ALIGN(4096);
    _end = .;
}

PHDRS
{
    code PT_LOAD FLAGS(5); /* PF_R|PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data PT_LOAD FLAGS(6); /* PF_R|PF_W */
    note PT_NOTE FLAGS(4); /* PF_R */
}
