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
    . = 0x01000000; /* start of user space */

    /* bump forward so the load offset and file offset are 1:1 */
    . += SIZEOF_HEADERS;

    .text : {
        *(.crt)
        *(.text* .gnu.linkonce.t.*)
    } =0x9090

    .interp : { *(.interp) }
    .hash : { *(.hash) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .rela.dyn : {
        *(.rela.init)
        *(.rela.text .rela.text.* .rela.gnu.linkonce.t.*)
        *(.rela.fini)
        *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*)
        *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*)
        *(.rela.tdata .rela.tdata.* .rela.gnu.linkonce.td.*)
        *(.rela.tbss .rela.tbss.* .rela.gnu.linkonce.tb.*)
        *(.rela.ctors)
        *(.rela.dtors)
        *(.rela.got)
        *(.rela.bss .rela.bss.* .rela.gnu.linkonce.b.*)
        *(.rela.ldata .rela.ldata.* .rela.gnu.linkonce.l.*)
        *(.rela.lbss .rela.lbss.* .rela.gnu.linkonce.lb.*)
        *(.rela.lrodata .rela.lrodata.* .rela.gnu.linkonce.lr.*)
        *(.rela.ifunc)
    }
    .rela.plt : {
        *(.rela.plt)
        PROVIDE_HIDDEN (__rela_iplt_start = .);
        *(.rela.iplt)
        PROVIDE_HIDDEN (__rela_iplt_end = .);
    }

    .init : { *(.init) } =0x9090
    .plt : { *(.plt) }

    .rodata : ALIGN(8) {
        __rodata_start = .;
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        __rodata_end = .;
    }

    .data : ALIGN(4096) {
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
    .got : { *(.got.plt) *(.got) }
    .data.rel.ro : { *(.data.rel.ro.local* .gnu.linkonce.d.rel.ro.local.*) *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) }
    .dynamic : { *(.dynamic) }

    .bss : ALIGN(8) {
        __bss_start = .;
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        __bss_end = .;
    }

    _end = .;

    /DISCARD/ : { *(.comment .note) }
}
