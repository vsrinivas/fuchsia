// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lookup.h"

#include <lib/page-table/arch/x86/builder.h>
#include <lib/page-table/types.h>

#include "mmu.h"

namespace page_table::x86 {
namespace {

// Return the page table level entries for pages of `size` are located in.
constexpr int8_t LevelForPageSize(PageSize size) {
  switch (size) {
    case PageSize::k4KiB:
      return 0;
    case PageSize::k2MiB:
      return 1;
    case PageSize::k1GiB:
      return 2;
  }
}

}  // namespace

std::optional<LookupResult> LookupPage(MemoryManager& allocator, PageTableNode* node,
                                       Vaddr virt_addr) {
  ZX_ASSERT(IsCanonicalVaddr(virt_addr));

  for (int8_t level = kPageTableLevels - 1; level >= 0; level--) {
    // Get the PTE for the current node.
    size_t index = (virt_addr.value() >> PageLevelBits(level)) % kEntriesPerNode;
    PageTableEntry entry = node->at(index);

    // If present bit is off, the entry is invalid.
    if (!entry.present()) {
      return std::nullopt;
    }

    // If this is a page, we have found the page.
    if (entry.is_page(level)) {
      uint64_t page_offset = virt_addr.value() & internal::Mask(PageLevelBits(level));
      return LookupResult{
          .phys_addr = Paddr(entry.page_paddr(level) | page_offset),
          .entry = entry,
          .level = level,
      };
    }

    // Otherwise, we keep walking down the tree.
    node = reinterpret_cast<PageTableNode*>(allocator.PhysToPtr(Paddr(entry.child_paddr())));
  }

  // Should have reached a page or non-present entry here.
  ZX_ASSERT_MSG(false, "Unreachable.");
  __UNREACHABLE;
}

zx_status_t MapPage(MemoryManager& allocator, PageTableNode* node, Vaddr virt_addr, Paddr phys_addr,
                    PageSize page_size) {
  ZX_ASSERT(phys_addr <= kMaxPhysAddress);
  ZX_ASSERT(IsCanonicalVaddr(virt_addr));
  ZX_ASSERT(virt_addr.value() % PageBytes(page_size) == 0);
  ZX_ASSERT(phys_addr.value() % PageBytes(page_size) == 0);

  // Determine which level the page should be mapped on.
  int8_t final_level = LevelForPageSize(page_size);

  for (int8_t level = kPageTableLevels - 1; level > final_level; level--) {
    // Get the PTE for the current node.
    size_t index = (virt_addr.value() >> PageLevelBits(level)) % kEntriesPerNode;
    PageTableEntry entry = node->at(index);

    // If there is already a page mapping here, abort.
    if (entry.present() && entry.is_page(level)) {
      return ZX_ERR_ALREADY_EXISTS;
    }

    // If present bit is off, allocate a new leaf node.
    if (!entry.present()) {
      auto new_node = reinterpret_cast<PageTableNode*>(
          allocator.Allocate(/*size=*/sizeof(PageTableNode), /*alignment=*/alignof(PageTableNode)));
      if (new_node == nullptr) {
        return ZX_ERR_NO_MEMORY;
      }
      new (new_node) PageTableNode();

      entry =
          PageTableEntry{}
              .set_present(1)
              .set_read_write(1)  // Allow writes.
              .set_is_page(level, false)
              .set_child_paddr(allocator.PtrToPhys(reinterpret_cast<std::byte*>(new_node)).value());
      node->set(index, entry);
    }

    // Move to the next level.
    node = reinterpret_cast<PageTableNode*>(allocator.PhysToPtr(Paddr(entry.child_paddr())));
  }

  // At the final level. Get the PTE.
  size_t index = (virt_addr.value() >> PageLevelBits(final_level)) % kEntriesPerNode;
  PageTableEntry entry = node->at(index);

  // If there is already a page mapping here, abort.
  if (entry.present()) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  // Add a new entry.
  node->set(index, PageTableEntry{}
                       .set_present(1)
                       .set_read_write(1)  // Allow writes.
                       .set_is_page(/*level=*/final_level, true)
                       .set_page_paddr(/*level=*/final_level, phys_addr.value()));
  return ZX_OK;
}
}  // namespace page_table::x86
