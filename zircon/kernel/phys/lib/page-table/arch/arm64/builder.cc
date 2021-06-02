// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "lib/page-table/arch/arm64/builder.h"

#include <lib/page-table/arch/arm64/mmu.h>
#include <lib/page-table/internal/bits.h>

#include <initializer_list>

#include "lookup.h"

namespace page_table::arm64 {

namespace {

using internal::IsAligned;

// Get the largest page size from the given list that can be used to map the
// beginning of the region `[vaddr, vaddr+size)` to `paddr`.
constexpr PageSize GetLargestPageSize(std::initializer_list<PageSize> sizes, Vaddr vaddr,
                                      Paddr paddr, uint64_t size) {
  // Get the maximum alignment of vaddr and paddr.
  uint64_t alignment = internal::MaxAlignmentBits(paddr.value() | vaddr.value());

  // Select the first page size that can be used.
  for (PageSize page_size : sizes) {
    if (alignment >= PageBits(page_size) && size >= PageBytes(page_size)) {
      return page_size;
    }
  }

  // Unaligned page.
  ZX_PANIC("Unaligned to any page size: vaddr=%" PRIx64 ", paddr=%" PRIx64 ", size=%" PRIu64,
           vaddr.value(), paddr.value(), size);
}

// Get the largest page size that can be used to map the beginning of the region
// `[vaddr, vaddr+size)` to `paddr`.
//
// All arguments must be aligned to at least the smallest page size.
constexpr PageSize GetLargestPageSize(PageTableLayout layout, Vaddr vaddr, Paddr paddr,
                                      uint64_t size) {
  switch (layout.granule_size) {
    case GranuleSize::k4KiB:
      return GetLargestPageSize({PageSize::k1GiB, PageSize::k2MiB, PageSize::k4KiB}, vaddr, paddr,
                                size);
    case GranuleSize::k16KiB:
      return GetLargestPageSize({PageSize::k32MiB, PageSize::k16KiB}, vaddr, paddr, size);
    case GranuleSize::k64KiB:
      return GetLargestPageSize({PageSize::k512MiB, PageSize::k64KiB}, vaddr, paddr, size);
  }
}

// Allocate a granule of the given size, and set it to zero.
//
// Return nullptr on allocation failure.
void* AllocateGranule(MemoryManager& allocator, GranuleSize granule_size) {
  const size_t size = GranuleBytes(granule_size);
  void* allocation = allocator.Allocate(/*size=*/size, /*alignment=*/size);
  if (allocation == nullptr) {
    return nullptr;
  }
  memset(allocation, 0, size);
  return allocation;
}

}  // namespace

std::optional<AddressSpaceBuilder> AddressSpaceBuilder::Create(MemoryManager& allocator,
                                                               const PageTableLayout& layout) {
  void* top_level = AllocateGranule(allocator, layout.granule_size);
  if (top_level == nullptr) {
    return std::nullopt;
  }
  return AddressSpaceBuilder(
      allocator, PageTableNode(reinterpret_cast<PageTableEntry*>(top_level), layout.granule_size),
      layout);
}

zx_status_t AddressSpaceBuilder::MapRegion(Vaddr virt_start, Paddr phys_start, uint64_t size) {
  // Zero-sized regions are trivially mapped.
  if (size == 0) {
    return ZX_OK;
  }

  // Ensure neither the physical or virtual address ranges overflow.
  uint64_t unused;
  if (add_overflow(virt_start.value(), size - 1, /*result=*/&unused)) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (add_overflow(phys_start.value(), size - 1, /*result=*/&unused)) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Ensure the range of virtual addresses is valid.
  if (virt_start.value() + size - 1 >= layout_.AddressSpaceSize()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Addresses must be aligned to at least the smallest page size.
  if (!IsAligned(virt_start.value(), GranuleBytes(layout_.granule_size))) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!IsAligned(phys_start.value(), GranuleBytes(layout_.granule_size))) {
    return ZX_ERR_INVALID_ARGS;
  }
  if (!IsAligned(size, GranuleBytes(layout_.granule_size))) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Map in everything a page at a time.
  while (size > 0) {
    // Get the biggest page size we can use.
    PageSize page_size = GetLargestPageSize(layout_, virt_start, phys_start, size);

    // Map it in.
    if (zx_status_t result =
            MapPage(allocator_, layout_, root_node_, virt_start, phys_start, page_size);
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

}  // namespace page_table::arm64
