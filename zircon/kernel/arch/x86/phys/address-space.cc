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
#include <ktl/optional.h>

namespace {

using page_table::Paddr;
using page_table::Vaddr;

void SwitchToPageTable(Paddr root) {
  // Disable support for global pages ("page global enable"), which
  // otherwise would not be flushed in the operation below.
  arch::X86Cr4::Read().set_pge(0).Write();

  // Set the new page table root. This will flush the TLB.
  arch::X86Cr3::Write(root.value());
}

}  // namespace

void InstallIdentityMapPageTables(page_table::MemoryManager& allocator,
                                  const zbitl::MemRangeTable& memory_map) {
  // Get the range of addresses in the memory map.
  //
  // It is okay if we over-approximate the required ranges, but we want to
  // ensure that all physical memory is in the range.
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
  zx_status_t result = builder->MapRegion(Vaddr(start), Paddr(start), end - start,
                                          page_table::CacheAttributes::kNormal);
  if (result != ZX_OK) {
    ZX_PANIC("Failed to map in range.");
  }

  // Switch to the new page table.
  SwitchToPageTable(builder->root_paddr());
}
