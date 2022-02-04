// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "address-space.h"

#include <lib/arch/x86/boot-cpuid.h>
#include <lib/arch/x86/system.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/page-table/builder.h>
#include <lib/page-table/types.h>

#include <fbl/algorithm.h>
#include <ktl/algorithm.h>
#include <ktl/optional.h>
#include <phys/allocation.h>

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

void InstallIdentityMapPageTables(page_table::MemoryManager& manager) {
  // Create a page table data structure.
  ktl::optional builder = page_table::AddressSpaceBuilder::Create(manager, arch::BootCpuidIo{});
  if (!builder.has_value()) {
    ZX_PANIC("Failed to create an AddressSpaceBuilder.");
  }

  const auto& pool = Allocation::GetPool();
  uint64_t first = fbl::round_down(pool.front().addr, ZX_MAX_PAGE_SIZE);
  uint64_t last = fbl::round_up(pool.back().end(), ZX_MAX_PAGE_SIZE);
  ZX_DEBUG_ASSERT(first < last);
  zx_status_t result = builder->MapRegion(Vaddr(first), Paddr(first), last - first,
                                          page_table::CacheAttributes::kNormal);
  if (result != ZX_OK) {
    ZX_PANIC("Failed to map in range.");
  }

  // Switch to the new page table.
  SwitchToPageTable(builder->root_paddr());
}
