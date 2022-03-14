// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lookup.h"

#include <lib/arch/riscv64/system.h>
#include <lib/page-table/arch/riscv64/mmu.h>
#include <lib/page-table/internal/bits.h>
#include <lib/page-table/types.h>

namespace page_table::riscv64 {

namespace {

using internal::IsAligned;

// Return true if the given entry is a page (either standard or large page).
bool IsLeaf(PageTableEntry entry) {
  return entry.r() || entry.x() || entry.w();
}

}  // namespace

zx_status_t MapPage(MemoryManager& allocator, const PageTableLayout& layout, PageTableNode node,
                    Vaddr virt_addr, Paddr phys_addr, PageSize page_size,
                    CacheAttributes cache_attrs) {
  ZX_ASSERT(phys_addr <= kMaxPhysAddress);
  ZX_ASSERT(virt_addr.value() < layout.AddressSpaceSize());
  ZX_ASSERT(IsAligned(virt_addr.value(), PageBytes(page_size)));
  ZX_ASSERT(IsAligned(phys_addr.value(), PageBytes(page_size)));

  // Ensure the page size is valid.
  if (GranuleForPageSize(page_size) != layout.granule_size) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (PageBits(page_size) > layout.region_size_bits) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Walk down the page table.
  ZX_DEBUG_ASSERT(layout.NumLevels() >= 1);
  for (uint64_t level = layout.NumLevels() - 1;; level--) {
    // Get the page table entry for this level.
    const uint64_t pte_range_bits = layout.PageTableEntryRangeBits(level);
    const size_t index = static_cast<size_t>((virt_addr.value() >> pte_range_bits) &
                                             internal::Mask(layout.TranslationBitsPerLevel()));
    PageTableEntry entry = node.at(index);

    // If there is already a page here, abort.
    if (entry.v() && IsLeaf(entry)) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    // If we've hit the final level, set up the page table entry.
    if (level == 0) {
      ZX_DEBUG_ASSERT(pte_range_bits == PageBits(page_size));
      node.set(index, PageTableEntry::PageAtAddress(phys_addr));
      return ZX_OK;
    }

    // If we've hit the correct level for a large page, set it up.
    if (pte_range_bits == PageBits(page_size)) {
      node.set(index, PageTableEntry::BlockAtAddress(phys_addr));
      return ZX_OK;
    }

    // If valid bit is off, allocate a new leaf node.
    if (!entry.v()) {
      auto new_node = reinterpret_cast<PageTableEntry*>(
          allocator.Allocate(/*size=*/GranuleBytes(layout.granule_size),
                             /*alignment=*/GranuleBytes(layout.granule_size)));
      if (new_node == nullptr) {
        return ZX_ERR_NO_MEMORY;
      }
      memset(new_node, 0, GranuleBytes(layout.granule_size));

      entry = PageTableEntry::TableAtAddress(
          allocator.PtrToPhys(reinterpret_cast<std::byte*>(new_node)));
      node.set(index, entry);
    }

    // Move to the next level.
    node = PageTableNode(
        reinterpret_cast<PageTableEntry*>(allocator.PhysToPtr(Paddr(entry.ppn() << 12))),
        layout.granule_size);
  }

  ZX_PANIC("unreachable");
}

}  // namespace page_table::riscv64
