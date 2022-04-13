// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_ARCH_X86_PHYS_INCLUDE_PHYS_ARCH_ARCH_ALLOCATION_H_
#define ZIRCON_KERNEL_ARCH_X86_PHYS_INCLUDE_PHYS_ARCH_ARCH_ALLOCATION_H_

#include <stdint.h>

#include <ktl/optional.h>

// The first 1MiB is reserved for 16-bit real-mode uses.
constexpr ktl::optional<uint64_t> kAllocationMinAddr = 1 << 20;

#endif  // ZIRCON_KERNEL_ARCH_X86_PHYS_INCLUDE_PHYS_ARCH_ARCH_ALLOCATION_H_
