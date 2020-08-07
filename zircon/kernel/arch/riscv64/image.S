// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <zircon/boot/image.h>


// This file lays out the final kernel image seen by the boot loader.
// It concatenates:
//     1. the boot loader headers
//     2. the actual kernel image (converted from the kernel ELF file)
//     3. the fixup code to relocate the kernel image
// The headers must tell the boot loader to load the whole combined image,
// and leave enough space in memory after it for the bss.
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

.text

// ZBI file header (zbi_header_t)
ZBI_CONTAINER_HEADER(_zbi_file_header, boot_load_end - _zbi_kernel_header)

// ZBI kernel header (zbi_header_t)
DATA(_zbi_kernel_header)
    .int ZBI_TYPE_KERNEL_RISCV64
    .int boot_load_end - _zbi_kernel_payload
    .int 0
    .int ZBI_FLAG_VERSION
    .int 0
    .int 0
    .int ZBI_ITEM_MAGIC
    .int ZBI_ITEM_NO_CRC32
END_DATA(_zbi_kernel_header)

// ZBI_TYPE_KERNEL payload (zbi_kernel_t)
DATA(_zbi_kernel_payload)
    // The boot-shim code expects this to be an offset from the beginning
    // of the load image, whatever the kernel's virtual address.
    .quad 0 // IMAGE_ELF_ENTRY - _zbi_file_header
    .quad 0 // IMAGE_MEMORY_END - boot_load_end
END_DATA(_zbi_kernel_payload)

// Pad out to the header size that was allocated in the kernel image layout.
// This ensures that the kernel image is aligned correctly in memory.
.org BOOT_HEADER_SIZE

// Include the kernel image itself, skipping the padding left for the headers.
DATA(kernel_image)
#include "kernel_image.h"
.incbin KERNEL_IMAGE, BOOT_HEADER_SIZE
DATA(kernel_image_end)
END_DATA(kernel_image)

// Immediately after the kernel image comes the fixup code.
// The start.S code sees this address as __data_end.

.macro fixup addr, n, stride
.endm

#include "kernel-fixups.inc"

.balign 8
DATA(boot_load_end)

// We don't use any scratch memory after the kernel's bss.
.globl IMAGE_RESERVE_SIZE
IMAGE_RESERVE_SIZE = 0
