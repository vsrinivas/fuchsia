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
  // Options for the builder.
  struct Options {
    // If true, use 1 GiB page mappings when possible. Only supported on some hardware.
    bool allow_1gib_pages = false;

    // Return default options.
    //
    // We can't use a default constructor due to LLVM bug
    // https://bugs.llvm.org/show_bug.cgi?id=36684
    static Options Default() { return Options{}; }
  };

  // Create a new AddressSpace builder, using the given allocator.
  static std::optional<AddressSpaceBuilder> Create(
      MemoryManager& allocator,
      const AddressSpaceBuilder::Options& options = AddressSpaceBuilder::Options::Default());

  // x86_64-specific page table root.
  PageTableNode* root_node() { return pml4_; }

  // |AddressSpaceBuilder| implementation.
  zx_status_t MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size) override;
  Paddr root_paddr() override { return allocator_.PtrToPhys(reinterpret_cast<std::byte*>(pml4_)); }

 private:
  explicit AddressSpaceBuilder(MemoryManager& allocator, PageTableNode* pml4,
                               const Options& options)
      : pml4_(pml4), allocator_(allocator), options_(options) {}

  PageTableNode* pml4_;
  MemoryManager& allocator_;
  Options options_;
};

}  // namespace page_table::x86

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_X86_BUILDER_H_
