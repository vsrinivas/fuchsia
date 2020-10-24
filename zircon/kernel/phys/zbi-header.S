// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>
#include <zircon/boot/image.h>

// The linker script (phys.ld) places this section first in the image.  It
// provides the ZBI headers for the kernel so that the whole load image is
// a standalone ZBI container of one (ZBI_TYPE_KERNEL_*) item.
.section .boot.header, "a", %progbits

ZBI_CONTAINER_HEADER(kZbiContainerHeader, PHYS_LOAD_END - kZbiItemHeader)

// zbi_header_t
.object kZbiItemHeader, rodata, nosection=nosection
#ifdef __aarch64__
  .int ZBI_TYPE_KERNEL_ARM64    // type
#elif defined(__x86_64__)
  .int ZBI_TYPE_KERNEL_X64      // type
#else
#error "what machine?"
#endif
  .int PHYS_LOAD_END - kZbiKernelHeader // length
  .int 0                                // extra
  .int ZBI_FLAG_VERSION                 // flags
  .int 0, 0                             // reserved0, reserved1
  .int ZBI_ITEM_MAGIC                   // magic
  .int ZBI_ITEM_NO_CRC32                // crc32
.end_object

#ifdef __x86_64__
#define kEntryBias 0x100000
#else
#define kEntryBias 0
#endif

// zbi_kernel_t
.object kZbiKernelHeader, rodata, nosection=nosection
  .quad _start - kZbiContainerHeader + kEntryBias
  .quad PHYS_RESERVE_MEMORY_SIZE
.end_object
