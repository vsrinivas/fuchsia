// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_PAGE_TABLE_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_PAGE_TABLE_H_

#include <lib/page-table/builder-interface.h>
#include <lib/page-table/types.h>

#include <ktl/byte.h>

// Forward-declared; fully declared in <lib/memalloc/allocator.h>.
namespace memalloc {
class Pool;
}  // namespace memalloc

// Perform architecture-specific address space set-up. The "Early" variant
// assumes that only the boot conditions hold and is expected to be called
// before "normal work" can proceed; otherwise, the "Late" variant assumes that
// we are in the opposite context and, in particular, that memory can be
// allocated such that it will not be clobbered before the next kernel sets up
// the address space again,
//
// In certain architectural contexts, early or late set-up will not make
// practical sense, and the associated functions may be no-ops.
void ArchSetUpAddressSpaceEarly();
void ArchSetUpAddressSpaceLate();

// Maps in the global UART's registers, assuming that they fit within a single
// page.
void MapUart(page_table::AddressSpaceBuilderInterface& builder, memalloc::Pool& pool);

// A page_table::MemoryManager that allocates by way of Allocator.
class AllocationMemoryManager final : public page_table::MemoryManager {
 public:
  explicit AllocationMemoryManager(memalloc::Pool& pool) : pool_(pool) {}

  ktl::byte* Allocate(size_t size, size_t alignment) final;

  page_table::Paddr PtrToPhys(ktl::byte* ptr) final {
    // We have a 1:1 virtual/physical mapping.
    return page_table::Paddr(reinterpret_cast<uintptr_t>(ptr));
  }

  ktl::byte* PhysToPtr(page_table::Paddr phys) final {
    // We have a 1:1 virtual/physical mapping.
    return reinterpret_cast<ktl::byte*>(static_cast<uintptr_t>(phys.value()));
  }

 private:
  memalloc::Pool& pool_;
};

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_PAGE_TABLE_H_
