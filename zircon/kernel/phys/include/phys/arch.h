// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ARCH_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ARCH_H_

#include <lib/memalloc.h>
#include <lib/zbitl/items/mem_config.h>

// Perform any architecture-specific set up.
void ArchSetUp();

// Perform any architecture-specific address space set up that needs to
// be done prior to using memory outside the program's .data / .bss / etc.
void ArchSetUpAddressSpace(memalloc::Allocator& allocator, const zbitl::MemRangeTable& table);

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ARCH_H_
