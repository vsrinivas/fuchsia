// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc/pool.h>
#include <lib/page-table/types.h>
#include <stdio.h>

#include <fbl/algorithm.h>
#include <ktl/array.h>
#include <ktl/byte.h>
#include <ktl/span.h>
#include <phys/allocation.h>
#include <phys/page-table.h>

#include "address-space.h"

namespace {

// On x86-64, we don't have any guarantee that all the memory in our address
// space is actually mapped in.
//
// We use a bootstrap allocator consisting of memory from ".bss" to construct a
// real page table with.  Unused memory will be returned to the heap after
// initialisation is complete.
//
// Amount of memory reserved in .bss for allocation of page table data
// structures: We reserve 512kiB. On machines which only support at most 2 MiB
// page sizes, we need ~8 bytes per 2 MiB, allowing us to map ~128 GiB of
// RAM. On machines with 1 GiB page sizes, we can support ~64 TiB of RAM.
//
constexpr size_t kBootstrapMemoryBytes = 512 * 1024;

// Bootstrap memory pool.
alignas(ZX_MIN_PAGE_SIZE) ktl::array<ktl::byte, kBootstrapMemoryBytes> gBootstrapMemory;

// A page_table::MemoryManager that uses a fixed range of memory, and
// assumes a 1:1 mapping from physical addresses to host virtual
// addresses.
class BootstrapMemoryManager final : public page_table::MemoryManager {
 public:
  explicit BootstrapMemoryManager(ktl::span<ktl::byte> memory) : memory_(memory) {}

  ktl::byte* Allocate(size_t size, size_t alignment) final {
    // Align up the next address.
    auto next_addr = reinterpret_cast<uintptr_t>(memory_.data());
    uintptr_t aligned_next_addr = fbl::round_up(next_addr, alignment);
    size_t padding = aligned_next_addr - next_addr;

    // Ensure there is enough space in the buffer.
    if (padding + size > memory_.size()) {
      printf("Cannot allocate %zu bytes @ %zu for bootstrap page tables!\n", size, alignment);
      return nullptr;
    }

    // Reserve the memory, and return the pointer.
    memory_ = memory_.subspan(alignment + padding);
    return reinterpret_cast<ktl::byte*>(aligned_next_addr);
  }

  page_table::Paddr PtrToPhys(ktl::byte* ptr) final {
    // We have a 1:1 virtual/physical mapping.
    return page_table::Paddr(reinterpret_cast<uint64_t>(ptr));
  }

  ktl::byte* PhysToPtr(page_table::Paddr phys) final {
    // We have a 1:1 virtual/physical mapping.
    return reinterpret_cast<ktl::byte*>(phys.value());
  }

  // Release all remaining memory into the allocator.
  void Release(memalloc::Pool& allocator) {
    if (!memory_.empty()) {
      if (allocator.Free(reinterpret_cast<uint64_t>(memory_.data()), memory_.size()).is_error()) {
        printf("Failed to release .bss bootstrap memory\n");
      }
    }
    memory_ = {};
  }

  ~BootstrapMemoryManager() { ZX_ASSERT(memory_.empty()); }

 private:
  ktl::span<ktl::byte> memory_;
};

}  // namespace

void ArchSetUpAddressSpaceEarly() {
  BootstrapMemoryManager manager(gBootstrapMemory);
  InstallIdentityMapPageTables(manager);
  manager.Release(Allocation::GetPool());
}

void ArchSetUpAddressSpaceLate() {
  AllocationMemoryManager manager(Allocation::GetPool());
  InstallIdentityMapPageTables(manager);
}
