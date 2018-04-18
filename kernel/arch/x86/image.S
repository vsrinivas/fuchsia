// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Google, Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/asm.h>
#include <zircon/boot/bootdata.h>
#include <zircon/boot/multiboot.h>


// This file lays out the final kernel image seen by the boot loader.
// It concatenates:
//     1. the boot loader headers
//     2. the actual kernel image (converted from the kernel ELF file)
//     3. the fixup code to relocate the kernel image
// The headers must tell the boot loader to load the whole combined image,
// and leave enough space in memory after it for the bss.  The fixup code
// in the image overlaps with the start of the kernel's bss, so start.S
// will move it to after the bss.  Hence the headers must tell the boot
// loader to leave enough space for that copy too.
//
// The label arithmetic to define the header fields only works because this
// whole file is all in the same section (.text).  Because it's all just
// one big section and there are no relocs to absolute locations within
// this section, it really doesn't matter what memory layout the linker
// thinks it's doing, but nonetheless image.ld produces an ELF segment
// layout faithful to the physical memory picture (except that it's
// actually position-independent).  The addresses in the ELF headers of the
// final image.elf file are completely ignored because boot loaders don't
// actually use that file.  It only exists to have the contents extracted
// with objcopy -O binary.

// Where the fixup code winds up in memory exactly overlaps the kernel's
// .bss, which the kernel needs to zero before it's ready to run the fixup
// code.  So move_fixups_and_zero_bss (in start.S) copies the fixup code to
// scratch memory starting at IMAGE_MEMORY_END (i.e., right after the
// kernel's .bss).  The "end of .bss" address in the multiboot header is
// the only way to reserve some space that the boot loader won't use for
// something like the kernel command like or the initial RAM disk.  So add
// the fixup code size.
//
// The bootdata header doesn't record this.  But the bootdata protocol from
// the bootloader ensures that the bootdata image is nowhere near the end of
// the kernel load image, so it doesn't need to know how much .bss space the
// kernel actually needs there.
#define boot_bss_end (IMAGE_MEMORY_END + (apply_fixups_end - apply_fixups))


.text

.balign 8
// BOOTDATA file header (bootdata_t)
DATA(_bootdata_file_header)
    .int BOOTDATA_CONTAINER
    .int boot_load_end - _bootdata_kernel_header
    .int BOOTDATA_MAGIC
    .int BOOTDATA_FLAG_V2
    .int 0
    .int 0
    .int BOOTITEM_MAGIC
    .int BOOTITEM_NO_CRC32
END_DATA(_bootdata_file_header)

// BOOTDATA kernel header (bootdata_t)
DATA(_bootdata_kernel_header)
    .int BOOTDATA_KERNEL
    .int boot_load_end - _bootdata_kernel_payload
    .int 0
    .int BOOTDATA_FLAG_V2
    .int 0
    .int 0
    .int BOOTITEM_MAGIC
    .int BOOTITEM_NO_CRC32
END_DATA(_bootdata_kernel_header)

// BOOTDATA_KERNEL payload (bootdata_kernel_t)
DATA(_bootdata_kernel_payload)
    .quad PHYS(IMAGE_ELF_ENTRY)
    .quad 0
END_DATA(_bootdata_kernel_payload)

.balign 8
DATA(_multiboot_header)
    .int MULTIBOOT_HEADER_MAGIC
    .int MULTIBOOT_HEADER_FLAGS
    .int -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
    .int PHYS_LOAD_ADDRESS + (_multiboot_header - _bootdata_file_header)
    .int PHYS_LOAD_ADDRESS
    .int PHYS(boot_load_end)
    .int PHYS(boot_bss_end)
    .int PHYS(IMAGE_MULTIBOOT_ENTRY)
END_DATA(_multiboot_header)

// Pad out to the header size that was allocated in the kernel image layout.
// This ensures that the kernel image is aligned correctly in memory.
.org BOOT_HEADER_SIZE

// Include the kernel image itself, skipping the padding left for the headers.
DATA(kernel_image)
.incbin KERNEL_IMAGE, BOOT_HEADER_SIZE
DATA(kernel_image_end)
END_DATA(kernel_image)

// Immediately after the kernel image comes the fixup code.
// The start.S code sees this address as _end.

// The first word encodes the size of the fixup code so it can be moved around.
DATA(fixup_code_size)
    .int apply_fixups_end - apply_fixups
END_DATA(fixup_code_size)

#define FIXUP_LOCATION(addr) (addr - KERNEL_BASE)(%rdi)

// This code must be purely position-independent and have no relocs.
// This is called with the runtime address of __code_start in %rdi.
FUNCTION(apply_fixups)
    mov %rdi, %rax
    sub $KERNEL_BASE, %rax

// The generated kernel-fixups.inc invokes this macro for each run of fixups.
.macro fixup addr, n, stride
.if \n == 1
    // This instruction is 7 bytes.
    add %rax, FIXUP_LOCATION(\addr)
.elseif \n == 2
    // So this pair is 14 bytes.
    add %rax, FIXUP_LOCATION(\addr)
    add %rax, FIXUP_LOCATION(\addr + 8)
.else
    // This sequence is 21 bytes, so it's smaller for n > 3.
    mov $\n, %ecx
    lea FIXUP_LOCATION(\addr), %rdx
0:
    add %rax, (%rdx)
    add $\stride, %rdx
    loop 0b
.endif
.endm

#include "zircon-fixups.inc"

    ret

DATA(apply_fixups_end)
END_FUNCTION(apply_fixups)

.balign 8
DATA(boot_load_end)
