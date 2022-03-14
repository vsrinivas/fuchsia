// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_RISCV64_LOOKUP_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_RISCV64_LOOKUP_H_

#include <lib/arch/riscv64/system.h>
#include <lib/page-table/arch/riscv64/mmu.h>
#include <lib/page-table/types.h>
#include <zircon/types.h>

#include <optional>

namespace page_table::riscv64 {

// Map a single page from `virt_addr` to `phys_addr`, allocating nodes as
// required.
//
// The page will be mapped as global with read/write/execute permissions.
//
// Returns ZX_ERR_NO_MEMORY on allocation failure.
//
// Returns ZX_ERR_ALREADY_EXISTS if part of the input range has already
// been mapped.
//
// TODO(fxbug.dev/67632): Add support for permissions.
zx_status_t MapPage(MemoryManager& allocator, const PageTableLayout& layout, PageTableNode node,
                    Vaddr virt_addr, Paddr phys_addr, PageSize page_size,
                    CacheAttributes cache_attrs);

}  // namespace page_table::riscv64

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_ARCH_RISCV64_LOOKUP_H_
