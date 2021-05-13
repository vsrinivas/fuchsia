// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/system.h>
#include <lib/page-table/builder.h>
#include <lib/page-table/types.h>
#include <lib/zbitl/items/mem_config.h>

#include <fbl/algorithm.h>
#include <ktl/algorithm.h>
#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <phys/arch.h>

namespace {

using page_table::Paddr;
using page_table::Vaddr;

// Amount of memory reserved in .bss for allocation of page table data structures.
//
// Unused memory will be returned to the heap after initialisation is complete.
//
// We reserve 512kiB. On machines which only support at most 2 MiB page
// sizes, we need ~8 bytes per 2 MiB, allowing us to map ~128 GiB of
// RAM. On machines with 1 GiB page sizes, we can support ~64 TiB of
// RAM.
constexpr size_t kBootstrapMemoryBytes = 512 * 1024;

// Bootstrap memory pool.
ktl::byte bootstrap_memory[kBootstrapMemoryBytes] __ALIGNED(ZX_MIN_PAGE_SIZE);

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
      return nullptr;
    }

    // Reserve the memory, and return the pointer.
    memory_ = memory_.subspan(alignment + padding);
    return reinterpret_cast<ktl::byte*>(aligned_next_addr);
  }

  Paddr PtrToPhys(ktl::byte* ptr) final {
    // We have a 1:1 virtual/physical mapping.
    return Paddr(reinterpret_cast<uint64_t>(ptr));
  }

  ktl::byte* PhysToPtr(Paddr phys) final {
    // We have a 1:1 virtual/physical mapping.
    return reinterpret_cast<ktl::byte*>(phys.value());
  }

  // Release all remaining memory into the given allocator.
  void Release(memalloc::Allocator& allocator) {
    printf("Releasing %" PRIu64 " bytes of early memory.\n", memory_.size());
    (void)allocator.AddRange(reinterpret_cast<uint64_t>(memory_.data()),
                             reinterpret_cast<uint64_t>(memory_.size()));
    memory_ = {};
  }

 private:
  ktl::span<ktl::byte> memory_;
};

void SwitchToPageTable(Paddr root) {
  // Disable support for global pages ("page global enable"), which
  // otherwise would not be flushed in the operation below.
  arch::X86Cr4::Read().set_pge(0).Write();

  // Set the new page table root. This will flush the TLB.
  arch::X86Cr3::Write(root.value());
}

void CreateBootstapPageTable(page_table::MemoryManager& allocator,
                             const zbitl::MemRangeTable& memory_map) {
  // Get the range of addresses in the memory map.
  //
  // It is okay if we over-approximate the required ranges, but we want to ensure
  // that all physical memory is in the range.
  uint64_t min_addr = UINT64_MAX;
  uint64_t max_addr = 0;
  size_t ranges = 0;
  for (const auto& range : memory_map) {
    min_addr = ktl::min(range.paddr, min_addr);
    max_addr = ktl::max(range.paddr + range.length, max_addr);
    ranges++;
  }

  // Ensure we encountered at least one range (and hence our memory
  // range is non-empty).
  if (ranges == 0) {
    ZX_PANIC("No memory ranges found.");
  }
  ZX_DEBUG_ASSERT(min_addr < max_addr);

  printf("Physical memory range 0x%" PRIx64 " -- 0x%" PRIx64 " (~%" PRIu64 " MiB)\n", min_addr,
         max_addr, (max_addr - min_addr) / 1024 / 1024);

  // Create a page table data structure.
  ktl::optional builder = page_table::AddressSpaceBuilder::Create(allocator, arch::BootCpuidIo{});
  if (!builder.has_value()) {
    ZX_PANIC("Failed to create an AddressSpaceBuilder.");
  }

  // Map in the physical range.
  uint64_t start = fbl::round_down(min_addr, ZX_MAX_PAGE_SIZE);
  uint64_t end = fbl::round_up(max_addr, ZX_MAX_PAGE_SIZE);
  zx_status_t result = builder->MapRegion(Vaddr(start), Paddr(start), end - start);
  if (result != ZX_OK) {
    ZX_PANIC("Failed to map in range.");
  }

  // Switch to the new page table.
  SwitchToPageTable(builder->root_paddr());
}

}  // namespace

void ArchSetUpAddressSpace(memalloc::Allocator& allocator, const zbitl::MemRangeTable& table) {
  // On x86, we don't have any guarantee that all the memory in our address space
  // is actually mapped in.
  //
  // We use a bootstrap allocator consisting of memory from ".bss" to construct a
  // real page table with.
  BootstrapMemoryManager bootstrap_allocator(bootstrap_memory);

  // Map in the address space.
  CreateBootstapPageTable(bootstrap_allocator, table);

  // Release unused bootsrap memory into the general pool.
  bootstrap_allocator.Release(allocator);
}
