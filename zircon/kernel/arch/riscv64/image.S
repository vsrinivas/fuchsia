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
    .quad IMAGE_ELF_ENTRY - 0xffff800000000000 # TODO(revest): Once the linker supports this relocation type add: - _zbi_file_header
    .quad IMAGE_MEMORY_END # TODO(revest): and here too: - boot_load_end
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

// TODO(revest): Use IMAGE_LOAD_START when LLVM supports it
// This constant was found in the boot-shim logs with the formula:
//   "Kernel to 0000000089180000" - "Kernel at 0000000088200000"
#define FIXUP_LOCATION(addr) (addr - KERNEL_BASE + 0xf80000)

// This code must be purely position-independent and have no relocs.
// This is called with the physical address of __code_start in a0 and the
// desired virtual address of __code_start in a1.
FUNCTION(apply_fixups)
    // This is the constant address the kernel was linked for.
    li   t1, KERNEL_BASE
    sub  t1, a1, t1

// The generated kernel-fixups.inc invokes this macro for each run of fixups.
.macro fixup addr, n, stride
    li  t2, FIXUP_LOCATION(\addr)
    add t2, t2, a0
    // TODO(revest): Optimize this macro to use less instructions when n < 3
    li   t3, \n
0:
    ld   t4, (t2)
    add  t4, t4, t1
    sd   t4, (t2)
    addi t2, t2, \stride
    addi t3, t3, -1
    bnez t3, 0b
.endm

#include "kernel-fixups.inc"

    ret

DATA(apply_fixups_end)
END_FUNCTION(apply_fixups)

.balign 8
DATA(boot_load_end)

// We don't use any scratch memory after the kernel's bss.
.globl IMAGE_RESERVE_SIZE
IMAGE_RESERVE_SIZE = 0
