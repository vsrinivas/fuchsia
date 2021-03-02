// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_X86_BUILDER_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_X86_BUILDER_H_

#include <lib/page-table/builder-interface.h>
#include <zircon/types.h>

#include <optional>

namespace page_table::x86 {

class PageTableNode;

class AddressSpaceBuilder final : public AddressSpaceBuilderInterface {
 public:
  // Create a new AddressSpace builder, using the given allocator.
  static std::optional<AddressSpaceBuilder> Create(MemoryManager& allocator);

  // x86_64-specific page table root.
  PageTableNode* root_node() { return pml4_; }

  // |AddressSpaceBuilder| implementation.
  zx_status_t MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size) override;
  Paddr root_paddr() override { return allocator_.PtrToPhys(reinterpret_cast<std::byte*>(pml4_)); }

 private:
  explicit AddressSpaceBuilder(MemoryManager& allocator, PageTableNode* pml4)
      : pml4_(pml4), allocator_(allocator) {}

  PageTableNode* pml4_;
  MemoryManager& allocator_;
};

}  // namespace page_table::x86

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_X86_BUILDER_H_
