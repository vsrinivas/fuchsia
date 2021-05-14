// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_ARM64_BUILDER_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_ARM64_BUILDER_H_

#include <lib/page-table/arch/arm64/mmu.h>
#include <lib/page-table/builder-interface.h>
#include <zircon/types.h>

#include <optional>
#include <utility>

namespace page_table::arm64 {

class AddressSpaceBuilder final : public AddressSpaceBuilderInterface {
 public:
  // Create a new AddressSpaceBuilder.
  static std::optional<AddressSpaceBuilder> Create(MemoryManager& allocator,
                                                   const PageTableLayout& layout);

  // ARM64-specific page table root.
  PageTableNode root_node() { return root_node_; }

  // Layout used by the builder.
  const PageTableLayout& layout() { return layout_; }

  // |AddressSpaceBuilder| implementation.
  zx_status_t MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size) override;
  Paddr root_paddr() override {
    return allocator_.PtrToPhys(reinterpret_cast<std::byte*>(root_node_.data()));
  }

 private:
  explicit AddressSpaceBuilder(MemoryManager& allocator, PageTableNode root_node,
                               const PageTableLayout& layout)
      : root_node_(root_node), allocator_(allocator), layout_(layout) {}

  PageTableNode root_node_;
  MemoryManager& allocator_;
  PageTableLayout layout_;
};

}  // namespace page_table::arm64

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_ARM64_BUILDER_H_
