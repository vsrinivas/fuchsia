// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_BOOT_SHIM_H_
#define ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_BOOT_SHIM_H_

#include <zircon/boot/image.h>

// This symbol is defined in boot-shim.ld.
extern zircon_kernel_t embedded_zbi;

// This type is tailored for the RISCV64 C ABI returning to assembly code.
typedef struct {
  zbi_header_t* zbi;  // Returned in a0.
  uint64_t entry;     // Returned in a1.
} boot_shim_return_t;

boot_shim_return_t boot_shim(uint64_t hart_id, void* device_tree);

#endif  // ZIRCON_KERNEL_TARGET_RISCV64_BOOT_SHIM_BOOT_SHIM_H_
