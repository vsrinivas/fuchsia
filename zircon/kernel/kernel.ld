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

/*
 * Symbols used in the kernel proper are defined with PROVIDE_HIDDEN:
 * HIDDEN because everything in the kernel is STV_HIDDEN to make it
 * clear that direct PC-relative references should be generated in PIC;
 * PROVIDE because their only purpose is to satisfy kernel references.
 */

SECTIONS
{
    . = KERNEL_BASE;
    PROVIDE_HIDDEN(__code_start = .);

    /*
     * This just leaves space in the memory image for the boot headers.
     * The actual boot header will be constructed in image.S, which see.
     */
    .text.boot0 : {
        /*
         * Put some data in, or else the linker makes it a SHT_NOBITS
         * section and that makes objcopy -O binary skip it in the image.
         */
        LONG(0xdeadbeef);
        . += BOOT_HEADER_SIZE - 4;
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

    /*
     * This is separate from .text just so gen-kaslr-relocs.sh can match
     * it.  The relocation processor skips this section because this code
     * all runs before the boot-time fixups are applied and has its own
     * special relationship with the memory layouts.
     */
    .text.boot : {
        *(.text.boot)
    }

    /*
     * This is separate from .text just so gen-kaslr-relocs.sh can match
     * it.  This section contains movabs instructions that get 64-bit
     * address fixups in place.  This is safe because this code is never
     * used until long after fixups have been applied.  In general, the
     * script will refuse to handle fixups in text (i.e. code) sections.
     */
    .text.bootstrap16 : {
        *(.text.bootstrap16)
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
    } :rodata

    /*
     * The named sections starting with kcountdesc are sorted by name so that
     * tools can provide binary search lookup for counters::Descriptor::name[]
     * variables.  This is page-aligned and padded out to page size so it can be
     * exported as a VMO without exposing any other .rodata contents.
     */
    .kcounter.desc : ALIGN(4096) {
        PROVIDE_HIDDEN(k_counter_desc_vmo_begin = .);
        KEEP(*(.kcounter.desc.header))
        ASSERT(. - k_counter_desc_vmo_begin == 16,
               "lib/counters/counters.cc and kernel.ld mismatch");
        QUAD(kcountdesc_end - kcountdesc_begin);
        PROVIDE_HIDDEN(kcountdesc_begin = .);
        ASSERT(kcountdesc_begin - k_counter_desc_vmo_begin == 24,
               "lib/counters/counters.cc and kernel.ld mismatch");
        KEEP(*(SORT_BY_NAME(kcountdesc.*)))
        PROVIDE_HIDDEN(kcountdesc_end = .);
        . = ALIGN(4096);
        PROVIDE_HIDDEN(k_counter_desc_vmo_end = .);
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
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)
               SORT_BY_INIT_PRIORITY(.ctors.*)))
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
     * just define it inside the writable section below, which is
     * exactly the end of the orphans rounded up to the next page.
     */

    __llvm_profile_header : ALIGN(4096) {
        PROVIDE_HIDDEN(__rodata_end = .);
        PROVIDE_HIDDEN(__data_start = .);

        /*
         * Keep this page-aligned/rounded so it can be exported as a VMO.
         * Unfortunately this means it all has to be writable, when really
         * only the __lvm_prf_cnts portion needs to be writable.  But it
         * would be wasteful to page-align and complicated to set up the
         * mappings to have this be both contiguous in the VMO and only
         * have the middle bit be writable in the kernel address space
         * (either by alternating read-only and writable pages here instead
         * of keeping the big rodata and data segments, or differently
         * complicated by placing the read-only and writable portions
         * discontiguously in the kernel's view but putting those physical
         * pages into the VMO in a different order.
         */
        PROVIDE_HIDDEN(__llvm_profile_start = .);
        KEEP(*(__llvm_profile_header))
        PROVIDE_HIDDEN(__start___llvm_prf_data = .);
    } :data
    __llvm_prf_data : {
        KEEP(*(__llvm_prf_data))
        PROVIDE_HIDDEN(__stop___llvm_prf_data = .);
    }
    __llvm_prf_cnts : {
        PROVIDE_HIDDEN(__start___llvm_prf_cnts = .);
        KEEP(*(__llvm_prf_cnts))
        PROVIDE_HIDDEN(__stop___llvm_prf_cnts = .);
    }
    __llvm_prf_names : {
        PROVIDE_HIDDEN(__start___llvm_prf_names = .);
        KEEP(*(__llvm_prf_names))
        PROVIDE_HIDDEN(__stop___llvm_prf_names = .);
    }
    PROVIDE_HIDDEN(__llvm_profile_end = .);
    . = ALIGN(4096);
    PROVIDE_HIDDEN(__llvm_profile_vmo_end = .);

    __sancov_guards : ALIGN(4) {
        PROVIDE_HIDDEN(__start___sancov_guards = .);
        *(__sancov_guards)
        PROVIDE_HIDDEN(__stop___sancov_guards = .);
    }

    .data : ALIGN(128) {
        /* Pull out any aligned data into a separate section to make sure
         * individual variables do not alias with any unaligned vars.
         */
        *(.data.cpu_align_exclusive)
        . = ALIGN(128);

        *(.data .data.* .gnu.linkonce.d.*)
    }

    /*
     * Note that this end size might not be aligned.  That's OK.  It's
     * not the actual end of the file size, because image.S adds on here
     * and it's responsible for ZBI item alignment at its own end.
     *
     * What *is* crucial here is that __data_end (i.e., .) not be advanced
     * without adding initialized data to fill!  Everything depends on the
     * __data_end address exactly matching the end of the raw kernel's
     * load image for relative address arithmetic.
     */
    PROVIDE_HIDDEN(__data_end = .);

    /*
     * Any writable orphan sections would be inserted here.
     * But there's no way to put the __data_end symbol after
     * them, so we cannot allow any such cases.  There is no
     * good way to assert that, though.
     */

    .bss : ALIGN(4096) {
        PROVIDE_HIDDEN(__bss_start = .);

        /*
         * See kernel/include/lib/counters.h; the KCOUNTER macro defines a
         * kcounter.NAME array in the .bss.kcounter.NAME section that allocates
         * SMP_MAX_CPUS counter slots.  Here we collect all those together to
         * make up the kcounters_arena contiguous array.  There is no particular
         * reason to sort these, but doing so makes them line up in parallel
         * with the sorted .kcounter.desc section.  Note that placement of the
         * input sections in the arena has no actual bearing on how the space is
         * used, because nothing ever refers to these arrays as variables--they
         * exist only to get the right amount of space allocated in the arena.
         * Instead, the order of the .kcounter.desc entries is what determines
         * how the arena is used: each index in the desc table corresponds to an
         * index in a per-CPU array, and the arena is a contiguous block of
         * SMP_MAX_CPUS such arrays.  The region containing the arena is
         * page-aligned and padded out to page size so that it can be exported
         * as a VMO without exposing any other .bss contents.
         */
        . = ALIGN(4096);
        PROVIDE_HIDDEN(kcounters_arena = .);
        KEEP(*(SORT_BY_NAME(.bss.kcounter.*)))
        PROVIDE_HIDDEN(kcounters_arena_end = .);
        . = ALIGN(4096);
        PROVIDE_HIDDEN(kcounters_arena_page_end = .);

        /*
         * Sanity check that the aggregate size of kcounters_arena SMP_MAX_CPUS
         * slots for each counter.  The counters::Descriptor structs in
         * .kcounter.desc are 64 bytes each.  (It's only for this sanity check
         * that we need to care how big counters::Descriptor is.)
         */
        ASSERT(kcounters_arena_end - kcounters_arena ==
               (kcountdesc_end - kcountdesc_begin) * 8 * SMP_MAX_CPUS / 64,
               "kcounters_arena size mismatch");

        /*
         * Keep this page-aligned/rounded so it can be exported as a VMO.
         */
        . = ALIGN(4096);
        PROVIDE_HIDDEN(__sancov_pc_table = .);
        . += (__stop___sancov_guards - __start___sancov_guards) / 4 * 8;
        PROVIDE_HIDDEN(__sancov_pc_table_end = .);
        . = ALIGN(4096);
        PROVIDE_HIDDEN(__sancov_pc_table_vmo_end = .);

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

/*
 * This is not actually used since the entry point is set in image.ld,
 * but it prevents the linker from warning about using a default address
 * and it keeps --gc-sections from removing .text.boot.
 */
ENTRY(IMAGE_ELF_ENTRY)

/*
 * These special symbols below are made public so they are visible via
 * --just-symbols to the link of image.S.
 */

IMAGE_LOAD_START = __code_start;
IMAGE_LOAD_END = __data_end;
IMAGE_MEMORY_END = _end;
