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
    . = KERNEL_BASE;
    PROVIDE_HIDDEN(__code_start = .);

    .text.boot0 : AT(MEMBASE + KERNEL_LOAD_OFFSET) {
        KEEP(*(.text.boot0))
    } :code

    . = ALIGN(8);
    .buildsig : {
        PROVIDE_HIDDEN(buildsig = .);
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

    PROVIDE_HIDDEN(__code_end = .);

    . = ALIGN(4096);
    PROVIDE_HIDDEN(__rodata_start = .);

    /*
     * These are page-aligned, so place them first.
     */
    .rodata.rodso_image : {
        *(.rodata.rodso_image.*)
    }

    .note.gnu.build-id : {
        PROVIDE_HIDDEN(__build_id_note_start = .);
        *(.note.gnu.build-id)
        PROVIDE_HIDDEN(__build_id_note_end = .);
    } :rodata :note

    .rodata : {
        *(.rodata* .gnu.linkonce.r.*)
    } :rodata

    /*
     * When compiling PIC, the compiler puts things into sections it
     * thinks need to be writable until after dynamic relocation.  In
     * the kernel, these things all go into the read-only segment.  But
     * to the linker, they are writable and so the default "orphans"
     * placement would put them after .data instead of here.  That's bad
     * both because we want these things in the read-only segment (the
     * kernel's self-relocation applies before the read-only-ness starts
     * being enforced anyway), and because the orphans would wind up
     * being after the __data_end symbol (see below).
     *
     * Therefore, we have to list all the special-case sections created
     * by __SECTION("foo") uses in the kernel that are RELRO candidates,
     * i.e. things that have address constants in their initializers.
     * All such uses in the source use sections named ".data.rel.ro.foo"
     * instead of just "foo" specifically to ensure we write them here.
     * This avoids the magic linker behavior for an "orphan" section
     * called "foo" of synthesizing "__start_foo" and "__stop_foo"
     * symbols when the section name has no . characters in it, and so
     * makes sure we'll get undefined symbol references if we omit such
     * a section here.  The magic linker behavior is nice, but it only
     * goes for orphans, and we can't abide the default placement of
     * orphans that should be RELRO.
     */
    .data.rel.ro : ALIGN(8) {
	PROVIDE_HIDDEN(__start_commands = .);
	KEEP(*(.data.rel.ro.commands))
	PROVIDE_HIDDEN(__stop_commands = .);

	PROVIDE_HIDDEN(__start_ktrace_probe = .);
	KEEP(*(.data.rel.ro.ktrace_probe))
	PROVIDE_HIDDEN(__stop_ktrace_probe = .);

	PROVIDE_HIDDEN(__start_lk_init = .);
	KEEP(*(.data.rel.ro.lk_init))
	PROVIDE_HIDDEN(__stop_lk_init = .);

	PROVIDE_HIDDEN(__start_lk_pdev_init = .);
	KEEP(*(.data.rel.ro.lk_pdev_init))
	PROVIDE_HIDDEN(__stop_lk_pdev_init = .);

	PROVIDE_HIDDEN(__start_unittest_testcases = .);
	KEEP(*(.data.rel.ro.unittest_testcases))
	PROVIDE_HIDDEN(__stop_unittest_testcases = .);

        *(.data.rel.ro* .gnu.linkonce.d.rel.ro.*)
    }

    .init_array : ALIGN(8) {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(.init_array .ctors))
        PROVIDE_HIDDEN(__init_array_end = .);
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
        PROVIDE_HIDDEN(__rodata_end = .);
        PROVIDE_HIDDEN(__data_start = .);
        *(.data .data.* .gnu.linkonce.d.*)
        PROVIDE_HIDDEN(__data_end = .);
    } :data

    /*
     * Any writable orphan sections would be inserted here.
     * But there's no way to put the __data_end symbol after
     * them, so we cannot allow any such cases.  There is no
     * good way to assert that, though.
     */

    .bss : ALIGN(4096) {
        PROVIDE_HIDDEN(__bss_start = .);
        KEEP(*(.bss.prebss.*))
        . = ALIGN(16);
        PROVIDE_HIDDEN(__post_prebss_bss_start = .);
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
    }

    /*
     * Any SHT_NOBITS (.bss-like) sections would be inserted here.
     */

     . = ALIGN(4096);
    PROVIDE_HIDDEN(_end = .);
}

PHDRS
{
    code PT_LOAD FLAGS(5); /* PF_R|PF_X */
    rodata PT_LOAD FLAGS(4); /* PF_R */
    data PT_LOAD FLAGS(6); /* PF_R|PF_W */
    note PT_NOTE FLAGS(4); /* PF_R */
}
