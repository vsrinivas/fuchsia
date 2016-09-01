/*
 * Copyright 2016 The Fuchsia Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * This is a linker script for producing a DSO (shared library) image
 * that is entirely read-only and trivial to map in without using a
 * proper ELF loader.  It has two segments: read-only starting at the
 * beginning of the file, and executable code page-aligned and marked
 * by the (hidden) symbols CODE_START and CODE_END.
 *
 * Ideally this could be accomplished without an explicit linker
 * script.  The linker would need an option to make the .dynamic
 * section (aka PT_DYNAMIC segment) read-only rather than read-write;
 * in fact that could be the default for Magenta/Fuchsia or for
 * anything using a dynamic linker like musl's that doesn't try to
 * write into the .dynamic section at runtime (for -shared that is;
 * for -pie and dynamically-linked executables there is the DT_DEBUG
 * question).  The linker would need a second option to entirely
 * segregate code from rodata (and from non-loaded parts of the file),
 * and page-align the code segment (and pad the end to a page
 * boundary); in fact that could be the default for any system that
 * wants to minimize what can go into pages mapped with execute
 * permission, which is a worthwhile trade-off of security mitigation
 * over tiny amounts of wasted space in the ELF file.  Beyond that,
 * the linker should not generate the .got* or .plt* sections at all
 * when there are no relocs being generated, but today's linkers still
 * do; since some of those sections are writable, they cause the
 * creation of a writable PT_LOAD segment by normal linker logic.
 */

SECTIONS {
    . = 0 + SIZEOF_HEADERS;

    /*
     * Match the non-allocated Gold version note specially, so
     * it doesn't go into the allocated .note section below.
     * With BFD ld, the .note clause could use:
     *     INPUT_SECTION_FLAGS(SHF_ALLOC) *(.note*)
     * so as not to match any non-allocated note sections generically.
     * But gold and lld do not support the INPUT_SECTION_FLAGS keyword.
     */
    .note.gnu.gold-version : { *(.note.gnu.gold-version) }

    .note : {
        *(.note*)
    } :rodata :note
    .dynamic : {
        *(.dynamic)
    } :rodata :dynamic
    .hash : {
        *(.hash)
    } :rodata
    .gnu_hash : { *(.gnu_hash) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .rela.plt : {
        *(.rela.plt*)
    }
    .rel.plt : {
        *(.rel.plt*)
    }
    .rela.dyn : {
        *(.rela.*)
    }
    .rel.dyn : {
        *(.rel.*)
    }
    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    } :rodata
    .rodata1 : { *(.rodata1) }
#ifdef __arm__
    .ARM.exidx : {
        *(.ARM.exidx*)
    } :rodata :exidx
    .ARM.extab : {
        *(.ARM.extab*)
    } :rodata
#else
    .eh_frame_hdr : {
        *(.eh_frame_hdr) *(.eh_frame_entry .eh_frame_entry.*)
    } :rodata :eh_frame_hdr
    .eh_frame : {
        KEEP(*(.eh_frame))
        *(.eh_frame.*)
    } :rodata
#endif
    .gcc_except_table : { *(.gcc_except_table*) }
    .gnu_extab : { *(.gnu_extab*) }

    /*
     * We'd like to discard these linker-generated sections with /DISCARD/
     * (or convince the linker not to generate them at all).
     * But the linker doesn't know how to do that.
     */
    .got : { *(.got*) }
    .plt : { *(.plt*) }

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    HIDDEN(CODE_START = .);

    .text : {
        *(.text.unlikely .text.*_unlikely .text.unlikely.*)
        *(.text.exit .text.exit.*)
        *(.text.startup .text.startup.*)
        *(.text.hot .text.hot.*)
        *(.text .stub .text.* .gnu.linkonce.t.*)
        *(.init .init.* .fini .fini.*)
        *(.gnu.warning)
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)

        PROVIDE_HIDDEN(_end = .);

        /*
         * Pad out the code segment to a page boundary, so that there
         * is only nop or zero padding visible in the memory image
         * rather than seeing non-loaded portions of the ELF file
         * (.shstrtab, section headers, .symtab if not stripped, etc.).
         */
        . = ALIGN(CONSTANT(MAXPAGESIZE));
        HIDDEN(CODE_END = .);
    } :code
}

/*
 * The linker doesn't understand the symbolic names for the
 * processor-specific p_type values.  So use explicit numbers.
 *
 * This is normally written (PT_LOPROC + 1), and that works in
 * BFD ld (i.e. cpp-expanded to (0x70000000 + 1)).  But gold only
 * supports a simple integer literal, not an arithmetic expression.
 */
#define PT_ARM_EXIDX 0x70000001

PHDRS {
    rodata PT_LOAD FLAGS(4) FILEHDR PHDRS;
    code PT_LOAD FLAGS(5);
    dynamic PT_DYNAMIC FLAGS(4);
    note PT_NOTE;
#ifdef __arm__
    exidx PT_ARM_EXIDX;
#else
    eh_frame_hdr PT_GNU_EH_FRAME;
#endif
}
