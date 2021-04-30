// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lookup.h"

#include <lib/arch/arm64/system.h>
#include <lib/page-table/arch/arm64/mmu.h>
#include <lib/page-table/internal/bits.h>
#include <lib/page-table/types.h>

namespace page_table::arm64 {

namespace {

// Return true if the given entry is a page (either standard or large page).
bool IsPage(uint64_t level, PageTableEntry entry) {
  if (level == 0) {
    return entry.type() == PageTableEntryType::kTableOrPageDescriptor;
  }
  return entry.type() == PageTableEntryType::kBlockDescriptor;
}

}  // namespace

std::optional<LookupPageResult> LookupPage(MemoryManager& allocator, const PageTableLayout& layout,
                                           PageTableNode node, Vaddr virt_addr) {
  // Ensure the virtual address lies within the range covered by the page table.
  if (virt_addr.value() >= layout.AddressSpaceSize()) {
    return std::nullopt;
  }

  // Walk down the page table.
  for (uint64_t level = layout.NumLevels() - 1;; level--) {
    // Get the page table entry for this level.
    const uint64_t pte_range_bits = layout.PageTableEntryRangeBits(level);
    const uint64_t index =
        (virt_addr.value() >> pte_range_bits) & internal::Mask(layout.TranslationBitsPerLevel());
    const PageTableEntry entry = node.at(index);

    // If the entry is not present, abort.
    if (!entry.present()) {
      return std::nullopt;
    }

    // If we are found a page, return it.
    if (IsPage(level, entry)) {
      uint64_t remaining_vaddr = virt_addr.value() & internal::Mask(pte_range_bits);
      uint64_t page_addr = entry.type() == PageTableEntryType::kBlockDescriptor
                               ? entry.as_block().address()
                               : entry.as_page().address();
      return LookupPageResult{
          .phys_addr = Paddr(page_addr | remaining_vaddr),
          .entry = entry,
          .page_size_bits = pte_range_bits,
      };
    }

    // If we are at the last level of the table, abort.
    if (level == 0) {
      return std::nullopt;
    }

    // Otherwise, we keep walking down the tree.
    node = PageTableNode(
        reinterpret_cast<PageTableEntry*>(allocator.PhysToPtr(Paddr(entry.as_table().address()))),
        layout.granule_size);
  }
}

}  // namespace page_table::arm64
