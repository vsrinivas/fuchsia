// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_X86_LOOKUP_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_X86_LOOKUP_H_

#include <lib/page-table/types.h>
#include <zircon/types.h>

#include <optional>

#include "mmu.h"

namespace page_table::x86 {

// Lookup the given page of the page table.
//
// No allocations will be performed, but the allocator is need to translate
// addresses.
struct LookupResult {
  // The physical address of `virt_addr`.
  Paddr phys_addr;

  // The PageTableEntry referencing the page.
  PageTableEntry entry;

  // The level the PageTableEntry is on.
  int8_t level;
};
std::optional<LookupResult> LookupPage(MemoryManager& allocator, PageTableNode* node,
                                       Vaddr virt_addr);

// Map a single page from `virt_addr` to `phys_addr`, allocating nodes as
// required.
//
// The page will be mapped with read/write/execute permissions, and
// using PAT entry 0.
//
// Returns ZX_ERR_NO_MEMORY on allocation failure.
//
// Returns ZX_ERR_ALREADY_EXISTS if part of the input range has already
// been mapped.
//
// TODO(fxbug.dev/67632): Add support for permissions, page attributes.
zx_status_t MapPage(MemoryManager& allocator, PageTableNode* node, Vaddr virt_addr, Paddr phys_addr,
                    PageSize page_size);

}  // namespace page_table::x86

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_X86_LOOKUP_H_
