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

// Return true if the beginning of the given vaddr/paddr/size range can
// be mapped with the given page size.
constexpr bool RegionMappableWithPageSize(Vaddr vaddr, Paddr paddr, uint64_t size,
                                          size_t page_size) {
  // If the region size is smaller than the page, we can't map it.
  if (size < page_size) {
    return false;
  }

  // We require both `paddr` and `vaddr` to be aligned to the proposed page size.
  return vaddr.value() % page_size == 0 && paddr.value() % page_size == 0;
}

// Get the largest page size that can be used to map the beginning of the region
// `[vaddr, vaddr+size)` to `paddr`.
//
// All arguments must be aligned to at least the smallet page size.
constexpr PageSize GetLargestPageSize(Vaddr vaddr, Paddr paddr, uint64_t size,
                                      bool use_1gib_mappings) {
  if (use_1gib_mappings && RegionMappableWithPageSize(vaddr, paddr, size, kPageSize1GiB)) {
    return PageSize::k1GiB;
  }
  if (RegionMappableWithPageSize(vaddr, paddr, size, kPageSize2MiB)) {
    return PageSize::k2MiB;
  }
  return PageSize::k4KiB;
}

}  // namespace

std::optional<AddressSpaceBuilder> AddressSpaceBuilder::Create(MemoryManager& allocator,
                                                               bool use_1gib_mappings) {
  void* top_level = allocator.Allocate(sizeof(PageTableNode), alignof(PageTableNode));
  if (top_level == nullptr) {
    return std::nullopt;
  }
  new (top_level) PageTableNode();
  return AddressSpaceBuilder(allocator, reinterpret_cast<PageTableNode*>(top_level),
                             /*use_1gib_mappings=*/use_1gib_mappings);
}

zx_status_t AddressSpaceBuilder::MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size,
                                           CacheAttributes cache_attrs) {
  // Zero-sized regions are trivially mapped.
  if (size == 0) {
    return ZX_OK;
  }

  // We currently only support normal mappings.
  //
  // TODO(fxbug.dev/67632): Add support for other attributes.
  if (cache_attrs != CacheAttributes::kNormal) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Ensure neither the physical or virtual address ranges overflow.
  uint64_t unused;
  if (add_overflow(virt_start.value(), size - 1, /*result=*/&unused)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (add_overflow(phys_start.value(), size - 1, /*result=*/&unused)) {
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
    PageSize page_size = GetLargestPageSize(virt_start, phys_start, size,
                                            /*use_1gib_mappings=*/use_1gib_mappings_);

    // Map it in.
    if (zx_status_t result = MapPage(allocator_, pml4_, virt_start, phys_start, page_size);
        result != ZX_OK) {
      return result;
    }

    size_t page_bytes = PageBytes(page_size);
    virt_start += page_bytes;
    phys_start += page_bytes;
    size -= page_bytes;
  }

  return ZX_OK;
}

}  // namespace page_table::x86
