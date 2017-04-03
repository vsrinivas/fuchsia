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
        KEEP(*(.text.boot.vectab))
        *(.text* .gnu.linkonce.t.*)
    }

    .dummy_post_text : {
        __code_end = .;
    }

    .rodata : ALIGN(4096) {
        __rodata_start = .;
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    }

    /*
     * extra linker scripts tend to insert sections just after .rodata,
     * so we want to make sure this symbol comes after anything inserted above,
     * but not aligned to the next section necessarily.
     */
    .dummy_post_rodata : {
        __rodata_end = .;
    }

    .data : ALIGN(4096) {
        /* writable data  */
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
