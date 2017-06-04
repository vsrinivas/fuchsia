/*
// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
*/

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-bigaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

ENTRY(_start)
SECTIONS
{
    . = %KERNEL_BASE% + %KERNEL_LOAD_OFFSET%;

    /* text/read-only data */
    /* set the load address to physical MEMBASE */
    .text : AT(%MEMBASE% + %KERNEL_LOAD_OFFSET%) {
        __code_start = .;
        KEEP(*(.text.boot))

        . = ALIGN(8);
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

        KEEP(*(.text.boot.vectab))
        *(.text* .gnu.linkonce.t.*)
    }

    .dummy_post_text : {
        __code_end = .;
    }

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
    }

    .init_array : ALIGN(8) {
        __init_array_start = .;
        KEEP(*(.init_array .ctors))
        __init_array_end = .;
    }
    .fini_array : ALIGN(8) {
        __fini_array_start = .;
        KEEP(*(.fini_array .dtors))
        __fini_array_end = .;
    }

    /*
     * extra linker scripts tend to insert sections just after .data,
     * so we want to make sure this symbol comes after anything inserted above,
     * but not aligned to the next section necessarily.
     */
    .dummy_post_data : {
        __data_end = .;
    }

    /* unintialized data (in same segment as writable data) */
    .bss : ALIGN(4096) {
        __bss_start = .;
        KEEP(*(.bss.prebss.*))
        . = ALIGN(16);
        __post_prebss_bss_start = .;
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    /* Align the end to ensure anything after the kernel ends up on its own pages */
    . = ALIGN(4096);
    _end = .;

    /* Strip unnecessary stuff */
    /DISCARD/ : { *(.comment .note) }
}
