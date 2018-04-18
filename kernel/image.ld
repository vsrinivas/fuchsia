/* Copyright 2018 The Fuchsia Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This is the linker script for the final load image of the kernel.  The
 * entire thing was assembled into a single .text section in one .o file
 * (see kernel/$ARCH/image.S).  The purpose of this link is to resolve
 * symbols used in image.S but defined in the kernel proper.  That's made
 * possible by using ld's --just-symbols switch to copy the symbols from
 * the kernel proper into this link.  That's how the boot loader headers in
 * image.S can encode things like the entry point address, the exact load
 * image size including KASLR fixup code, and the "end of .bss" memory
 * reservation size.
 *
 * Boot loaders use only the raw binary load image extracted from this
 * link, not the ELF file itself; image.S encodes the platform-specific
 * headers the boot loaders look for at the beginning of the raw binary.
 * Setting the .text address to its physical address is nice for looking at
 * the ELF file and debugging the boot sequence, but it has no effect on
 * the load image.  However, other loaders (e.g. qemu) actually use the ELF
 * file container of the load image to guide their loading.  So we need
 * this linker script to produce a PT_LOAD with the right p_paddr.
 */

ENTRY(IMAGE_ELF_ENTRY)

SECTIONS {
    . = IMAGE_LOAD_START;

    .load_image : {
        KEEP(*(.text))
    } :load_image

    /*
     * When a boot loader is actually using the ELF headers, it needs to
     * know how much memory to reserve after the load image (p_filesz is
     * the load image, and p_memsz > p_filesz to indicate the extra space
     * to reserve).  This ensures that the segment has the right p_memsz.
     */
    .bss : {
        . += ABSOLUTE(IMAGE_MEMORY_END) - ABSOLUTE(.);
	ASSERT(ABSOLUTE(.) == ABSOLUTE(IMAGE_MEMORY_END), "image layout bad");
    }
}

PHDRS {
    load_image PT_LOAD FLAGS(7); /* PF_R|PF_W|PF_X */
}
