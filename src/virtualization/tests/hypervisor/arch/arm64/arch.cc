// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/hypervisor/arch.h"

#include <lib/page-table/builder.h>
#include <lib/stdcompat/span.h>

#include <fbl/algorithm.h>

#include "lib/page-table/arch/arm64/builder.h"
#include "lib/page-table/arch/arm64/mmu.h"
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
  GuestMemoryManager(cpp20::span<uint8_t> guest_memory, GuestPaddr allocation_addr,
                     size_t free_region_size)
      : guest_memory_(guest_memory),
        next_allocation_(allocation_addr),
        free_region_end_(allocation_addr + free_region_size) {}

  // Get the physical address of the given pointer.
  GuestPaddr PtrToPhys(std::byte* ptr) override {
    auto ptr_addr = reinterpret_cast<uintptr_t>(ptr);
    auto guest_addr = reinterpret_cast<uintptr_t>(guest_memory_.data());
    ZX_ASSERT(ptr_addr >= guest_addr);
    ZX_ASSERT(ptr_addr - guest_addr < guest_memory_.size());
    return GuestPaddr{ptr_addr - guest_addr};
  }

  // Get a pointer to the given physical address.
  std::byte* PhysToPtr(GuestPaddr phys) override {
    ZX_ASSERT(phys.value() < guest_memory_.size());
    return reinterpret_cast<std::byte*>(guest_memory_.data() + phys.value());
  }

  // Allocate memory with the given size / alignment.
  std::byte* Allocate(size_t size, size_t alignment) override {
    // Align to requested alignment.
    GuestPaddr allocation_start = GuestPaddr(fbl::round_up(next_allocation_.value(), alignment));
    GuestPaddr allocation_end = allocation_start + size;

    // Ensure we didn't overflow during either the alignment step or the addition.
    if (allocation_start < next_allocation_ || allocation_end < allocation_start) {
      return nullptr;
    }

    // Ensure we haven't run out of space.
    if (allocation_end > free_region_end_) {
      return nullptr;
    }

    // Record that the memory has been used.
    next_allocation_ = allocation_end;

    return PhysToPtr(allocation_start);
  }

 private:
  cpp20::span<uint8_t> guest_memory_;
  GuestPaddr next_allocation_;
  GuestPaddr free_region_end_;
};

void SetUpGuestPageTable(cpp20::span<uint8_t> guest_memory) {
  page_table::arm64::PageTableLayout layout = {
      .granule_size = page_table::arm64::GranuleSize::k4KiB,
      .region_size_bits = REGION_SIZE_BITS,
  };

  // Set up a page table builder.
  GuestMemoryManager manager(guest_memory, /*allocation_addr=*/GuestPaddr(PAGE_TABLE_PADDR),
                             /*free_region_size=*/PAGE_TABLE_SIZE);
  auto builder = page_table::arm64::AddressSpaceBuilder::Create(manager, layout);
  ZX_ASSERT(builder.has_value());
  ZX_ASSERT(builder->root_paddr().value() == PAGE_TABLE_PADDR);

  // Map virtual memory 1:1 to physical memory.
  zx_status_t status =
      builder->MapRegion(GuestVaddr{0}, GuestPaddr{0}, fbl::roundup_pow2(guest_memory.size()),
                         page_table::CacheAttributes::kNormal);
  ZX_ASSERT(status == ZX_OK);
}
