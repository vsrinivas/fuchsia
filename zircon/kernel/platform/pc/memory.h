// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PLATFORM_PC_MEMORY_H_
#define ZIRCON_KERNEL_PLATFORM_PC_MEMORY_H_

#include <zircon/types.h>

// Reserve a region of memory used for MMIO device in early boot.
//
// May only be used in early boot, prior to the heap being initialised.
void mark_mmio_region_to_reserve(uint64_t base, size_t len);

// Reserve a range of IO ports.
//
// May only be used in early boot, prior to the heap being initialised.
void mark_pio_region_to_reserve(uint64_t base, size_t len);

#endif  // ZIRCON_KERNEL_PLATFORM_PC_MEMORY_H_
