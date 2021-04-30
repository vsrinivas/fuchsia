// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_ARM64_LOOKUP_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_ARM64_LOOKUP_H_

#include <lib/arch/arm64/system.h>
#include <lib/page-table/arch/arm64/mmu.h>
#include <lib/page-table/types.h>
#include <zircon/types.h>

#include <optional>

namespace page_table::arm64 {

// Lookup the given page of the page table.
//
// No allocations will be performed, but the allocator is need to translate
// addresses.
struct LookupPageResult {
  // The physical address of `virt_addr`.
  Paddr phys_addr;

  // The PageTableEntry referencing the page.
  PageTableEntry entry;

  // The size of the page (in bits) of this entry.
  uint64_t page_size_bits;
};
std::optional<LookupPageResult> LookupPage(MemoryManager& allocator, const PageTableLayout& layout,
                                           PageTableNode node, Vaddr virt_addr);

}  // namespace page_table::arm64

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_ARM64_LOOKUP_H_
