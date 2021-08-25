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

using internal::IsAligned;

// Return true if the given entry is a page (either standard or large page).
bool IsPage(uint64_t level, PageTableEntry entry) {
  if (level == 0) {
    return entry.type() == PageTableEntryType::kTableOrPageDescriptor;
  }
  return entry.type() == PageTableEntryType::kBlockDescriptor;
}

// Get the Memory Attribute Indirection Register (MAIR) index of the given
// CacheAttribute.
//
// The MAIR is installed globally, and maps indices specified in a page table
// entry to a set of cache attributes.
size_t ToMairIndex(CacheAttributes cache_attrs) {
  size_t index = static_cast<size_t>(cache_attrs);
  ZX_DEBUG_ASSERT(index < arch::ArmMemoryAttrIndirectionRegister::kNumAttributes);
  return index;
}

}  // namespace

arch::ArmMemoryAttrIndirectionRegister GetArmMemoryAttrIndirectionRegister() {
  return arch::ArmMemoryAttrIndirectionRegister::Get()
      .FromValue(0)
      .SetAttribute(ToMairIndex(CacheAttributes::kNormal), arch::ArmMemoryAttribute::kNormalCached)
      .SetAttribute(ToMairIndex(CacheAttributes::kDevice), arch::ArmMemoryAttribute::kDevice_nGnRE);
}

std::optional<LookupPageResult> LookupPage(MemoryManager& allocator, const PageTableLayout& layout,
                                           PageTableNode node, Vaddr virt_addr) {
  // Ensure the virtual address lies within the range covered by the page table.
  if (virt_addr.value() >= layout.AddressSpaceSize()) {
    return std::nullopt;
  }

  // Walk down the page table.
  ZX_DEBUG_ASSERT(layout.NumLevels() >= 1);
  for (uint64_t level = layout.NumLevels() - 1;; level--) {
    // Get the page table entry for this level.
    const uint64_t pte_range_bits = layout.PageTableEntryRangeBits(level);
    const size_t index = static_cast<size_t>((virt_addr.value() >> pte_range_bits) &
                                             internal::Mask(layout.TranslationBitsPerLevel()));
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

  // Calculate the desired attributes for the page.
  PteLowerAttrs lower_attrs = PteLowerAttrs{}
                                  .set_sh(Shareability::kInnerShareable)
                                  .set_attr_indx(ToMairIndex(cache_attrs))
                                  .set_ap(PagePermissions::kSupervisorReadWrite)
                                  .set_af(1);

  // Walk down the page table.
  ZX_DEBUG_ASSERT(layout.NumLevels() >= 1);
  for (uint64_t level = layout.NumLevels() - 1;; level--) {
    // Get the page table entry for this level.
    const uint64_t pte_range_bits = layout.PageTableEntryRangeBits(level);
    const size_t index = static_cast<size_t>((virt_addr.value() >> pte_range_bits) &
                                             internal::Mask(layout.TranslationBitsPerLevel()));
    PageTableEntry entry = node.at(index);

    // If there is already a page here, abort.
    if (entry.present() && IsPage(level, entry)) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    // If we've hit the final level, set up the page table entry.
    if (level == 0) {
      ZX_DEBUG_ASSERT(pte_range_bits == PageBits(page_size));
      PageTableEntry new_entry = PageTableEntry::PageAtAddress(phys_addr);
      new_entry.as_page().set_lower_attrs(lower_attrs);
      node.set(index, new_entry);
      return ZX_OK;
    }

    // If we've hit the correct level for a large page, set it up.
    if (pte_range_bits == PageBits(page_size)) {
      PageTableEntry new_entry = PageTableEntry::BlockAtAddress(phys_addr);
      new_entry.as_block().set_lower_attrs(lower_attrs);
      node.set(index, new_entry);
      return ZX_OK;
    }

    // If present bit is off, allocate a new leaf node.
    if (!entry.present()) {
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
        reinterpret_cast<PageTableEntry*>(allocator.PhysToPtr(Paddr(entry.as_table().address()))),
        layout.granule_size);
  }

  ZX_PANIC("unreachable");
}

}  // namespace page_table::arm64
