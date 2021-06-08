// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_X86_BUILDER_H_
#define ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_X86_BUILDER_H_

#include <lib/arch/x86/cpuid.h>
#include <lib/page-table/builder-interface.h>
#include <zircon/types.h>

#include <optional>
#include <utility>

namespace page_table::x86 {

class PageTableNode;

class AddressSpaceBuilder final : public AddressSpaceBuilderInterface {
 public:
  // Create a new AddressSpaceBuilder, deriving options suitable for the
  // system described by the given CpuidIoProvider.
  template <typename CpuidIoProvider>
  static std::optional<AddressSpaceBuilder> Create(MemoryManager& allocator, CpuidIoProvider&& io) {
    bool use_1gib_mappings = io.template Read<arch::CpuidAmdFeatureFlagsD>().page1gb() != 0;
    return Create(allocator, /*use_1gib_mappings=*/use_1gib_mappings);
  }

  // x86_64-specific page table root.
  PageTableNode* root_node() { return pml4_; }

  // |AddressSpaceBuilder| implementation.
  zx_status_t MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size,
                        CacheAttributes cache_attrs) override;
  Paddr root_paddr() override { return allocator_.PtrToPhys(reinterpret_cast<std::byte*>(pml4_)); }

 private:
  // Create a new AddressSpaceBuilder, using the given allocator and options.
  static std::optional<AddressSpaceBuilder> Create(MemoryManager& allocator,
                                                   bool use_1gib_mappings);

  explicit AddressSpaceBuilder(MemoryManager& allocator, PageTableNode* pml4,
                               bool use_1gib_mappings)
      : pml4_(pml4), allocator_(allocator), use_1gib_mappings_(use_1gib_mappings) {}

  PageTableNode* pml4_;
  MemoryManager& allocator_;

  // Use 1 GiB page mappings when possible. Not supported on all hardware.
  bool use_1gib_mappings_;
};

}  // namespace page_table::x86

#endif  // ZIRCON_KERNEL_PHYS_LIB_PAGE_TABLE_INCLUDE_LIB_PAGE_TABLE_ARCH_X86_BUILDER_H_
