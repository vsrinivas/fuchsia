// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/page-table/arch/x86/builder.h>

#include "lookup.h"
#include "mmu.h"

namespace page_table::x86 {

namespace {

// Get the largest page size that can be used to map the beginning of the region
// `[vaddr, vaddr+size)` to `paddr`.
//
// All arguments must be aligned to at least the smallet page size.
constexpr PageSize GetLargestPageSize(Vaddr vaddr, Paddr paddr, size_t size) {
  for (int i = kPageSizes.size() - 1; i > 0; i--) {
    size_t page_size = PageBytes(kPageSizes[i]);
    if (page_size <= size && vaddr.value() % page_size == 0 && paddr.value() % page_size == 0) {
      return kPageSizes[i];
    }
  }
  return kPageSizes[0];
}

}  // namespace

std::optional<AddressSpaceBuilder> AddressSpaceBuilder::Create(MemoryManager& allocator) {
  void* top_level = allocator.Allocate(sizeof(PageTableNode), alignof(PageTableNode));
  if (top_level == nullptr) {
    return std::nullopt;
  }
  new (top_level) PageTableNode();
  return AddressSpaceBuilder(allocator, reinterpret_cast<PageTableNode*>(top_level));
}

zx_status_t AddressSpaceBuilder::MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size) {
  // Ensure neither the physical or virtual address ranges overflow.
  uint64_t unused;
  if (add_overflow(virt_start.value(), size, /*result=*/&unused)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (add_overflow(phys_start.value(), size, /*result=*/&unused)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Ensure that the range of virtual addresses are all in canonical form.
  //
  // We do this by making sure that both the first and last virtual
  // addresses are canonical, and that `size` is small enough that the
  // range can't jump from one side of the canonical address range to
  // the other.
  if (!IsCanonicalVaddr(virt_start) || !IsCanonicalVaddr(virt_start + Vaddr(size - 1))) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (size >= (uint64_t(1) << kVirtAddressBits)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Addresses must be aligned to at least the smallest page size.
  if (virt_start.value() % kPageSize4KiB != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (phys_start.value() % kPageSize4KiB != 0) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (size % kPageSize4KiB != 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Map in everything a page at a time.
  while (size > 0) {
    // Get the biggest page size we can use.
    PageSize page_size = GetLargestPageSize(virt_start, phys_start, size);

    // Map it in.
    if (zx_status_t result = MapPage(allocator_, pml4_, virt_start, phys_start, page_size);
        result != ZX_OK) {
      return result;
    }

    virt_start += Vaddr(PageBytes(page_size));
    phys_start += Paddr(PageBytes(page_size));
    size -= PageBytes(page_size);
  }

  return ZX_OK;
}

}  // namespace page_table::x86
