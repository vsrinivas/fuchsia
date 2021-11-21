// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/hypervisor/arch.h"

#include <lib/page-table/builder.h>

#include <fbl/algorithm.h>
#include <fbl/span.h>

#include "lib/page-table/types.h"
#include "src/virtualization/tests/hypervisor/constants.h"
#include "src/virtualization/tests/hypervisor/hypervisor_tests.h"

// The page_table library physical addresses and virtual addresses are from
// the perspective of the guest.
using GuestPaddr = page_table::Paddr;
using GuestVaddr = page_table::Vaddr;

// Maps guest virtual/physical memory to host, and allocates guest physical memory
// for page tables.
class GuestMemoryManager : public page_table::MemoryManager {
 public:
  GuestMemoryManager(fbl::Span<uint8_t> guest_memory, GuestPaddr allocation_addr,
                     size_t free_region_size)
      : {}

  // Get the physical address of the given pointer.
  GuestPaddr PtrToPhys(std::byte* ptr) override {
    return 0;
  }

  // Get a pointer to the given physical address.
  std::byte* PhysToPtr(GuestPaddr phys) override {
    return nullptr;
  }

  // Allocate memory with the given size / alignment.
  std::byte* Allocate(size_t size, size_t alignment) override {
    return nullptr;
  }
};

void SetUpGuestPageTable(fbl::Span<uint8_t> guest_memory) {
}
