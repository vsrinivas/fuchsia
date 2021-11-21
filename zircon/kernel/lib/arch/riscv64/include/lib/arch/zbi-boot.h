// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_ZBI_BOOT_H_
#define ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_ZBI_BOOT_H_

#include <zircon/boot/image.h>

#include <cstdint>

namespace arch {

constexpr uint32_t kZbiBootKernelType = ZBI_TYPE_KERNEL_RISCV64;

// Alignment required for kernel ZBI passed to arch::ZbiBoot.
constexpr uintptr_t kZbiBootKernelAlignment = 1 << 16;

// Alignment required for data ZBI passed to arch::ZbiBoot.
constexpr uintptr_t kZbiBootDataAlignment = 1 << 12;

// Hand off to a ZBI kernel already loaded in memory.  The kernel and data ZBIs
// are already loaded at arbitrary physical addresses.  The kernel's address
// must be aligned to 64K and the data ZBI to 4K, as per the ZBI spec.  This
// can be called in physical address mode or with identity mapping that covers
// at least the kernel plus its reserve_memory_size and the whole data ZBI.
[[noreturn]] inline void ZbiBoot(zircon_kernel_t* kernel, zbi_header_t* zbi) {
  // Clear the stack and frame pointers and the link register so no misleading
  // breadcrumbs are left.
  uintptr_t entry = reinterpret_cast<uintptr_t>(kernel) + kernel->data_kernel.entry;
  __asm__ volatile(
      R"""(
      mv a0, %[zbi]
      jal %[entry]
      )"""
      :
      : [entry] "r"(entry), [zbi] "r"(zbi)
      :);

  // TODO(revest) jump to kernel
  __builtin_unreachable();
}

}  // namespace arch

#endif  // ZIRCON_KERNEL_LIB_ARCH_RISCV64_INCLUDE_LIB_ARCH_ZBI_BOOT_H_
