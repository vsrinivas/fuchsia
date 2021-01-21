// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "memory.h"

#include <inttypes.h>
#include <lib/memalloc.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <fbl/no_destructor.h>
#include <ktl/optional.h>

#include "main.h"

namespace {

// Maximum number of address ranges we support reading from the
// previous-stage bootloader.
constexpr size_t kMaxMemoryRanges = 128;

using ZbiView = zbitl::View<zbitl::ByteView>;

// Ensure that the given ZbiView result is not an error.
//
// If an error, panic.
void AssertNoError(fitx::result<ZbiView::Error> result) {
  if (unlikely(result.is_error())) {
    ZX_PANIC("Error while scanning memory ranges: %.*s\n",
             static_cast<int>(result.error_value().zbi_error.size()),
             result.error_value().zbi_error.data());
  }
}

// Convert a zbi_mem_range_t memory type into a human-readable string.
const char* RangeTypeString(uint32_t type) {
  switch (type) {
    case ZBI_MEM_RANGE_RAM:
      return "RAM";
    case ZBI_MEM_RANGE_PERIPHERAL:
      return "peripheral";
    case ZBI_MEM_RANGE_RESERVED:
      return "reserved";
    default:
      return "unknown";
  }
}

// Print all memory ranges in the given ZbiView.
void PrintMemoryRanges(const ZbiView& view) {
  zbitl::MemRangeTable container{view};
  printf("Memory ranges present in ZBI:\n");
  for (const auto& range : container) {
    printf("  paddr: [0x%16" PRIx64 " -- 0x%16" PRIx64 ") : size %10" PRIu64 " kiB : %s\n",
           range.paddr, range.paddr + range.length, range.length / 1024,
           RangeTypeString(range.type));
  }
  printf("\n");
  AssertNoError(container.take_error());
}

// Remove architecture-specific regions of memory.
void ArchRemoveReservedRanges(memalloc::Allocator& allocator) {
#if defined(__x86_64__)
  // On x86_64, remove space likely to be holding our page tables. We assume
  // here that the page tables are contiguously allocated, starting at CR3,
  // and all fitting within 1MiB.
  //
  // TODO(fxb/67632): This is a temporary hack to make this work on x86.
  // Longer term, we plan to allocate new page tables and switch into those
  // instead of attempting to find the existing ones.
  //
  // TODO(fxb/67631): Move architecture-specific code into arch/ directories.
  {
    // Get top-level page directory location, stored in the CR3 register.
    uint64_t cr3;
    __asm__("mov %%cr3, %0" : "=r"(cr3));

    // Remove the range.
    constexpr uint64_t kMiB = 1024 * 1024;
    zx::status<> result = allocator.RemoveRange(cr3, 1 * kMiB);
    ZX_ASSERT(result.is_ok());
  }

  // On x86-64, remove space unlikely to be mapped into our address space (anything past 1 GiB).
  constexpr uint64_t kGiB = 1024 * 1024 * 1024;
  zx::status<> result = allocator.RemoveRange(1 * kGiB, UINT64_MAX - 1 * kGiB + 1);
  ZX_ASSERT(result.is_ok());
#endif
}

// Global memory allocation book-keeping.
memalloc::Allocator& GetAllocator() {
  static memalloc::RangeStorage allocator_storage[kMaxMemoryRanges];
  // Use fbl::NoDestructor to avoid generation of static destructors,
  // which fails in the phys environment.
  static fbl::NoDestructor<memalloc::Allocator> allocator(allocator_storage);
  return *allocator;
}

}  // namespace

void InitMemory(const zbi_header_t* zbi) {
  zbitl::View<zbitl::ByteView> view{zbitl::StorageFromRawHeader(zbi)};

  // Print memory information.
  PrintMemoryRanges(view);

  // Add all memory claimed to be free to the allocator.
  memalloc::Allocator& allocator = GetAllocator();
  zbitl::MemRangeTable container{view};
  for (const auto& range : container) {
    // Ignore reserved memory on our first pass.
    if (range.type != ZBI_MEM_RANGE_RAM) {
      continue;
    }
    zx::status<> result = allocator.AddRange(range.paddr, range.length);
    ZX_ASSERT(result.is_ok());
  }
  AssertNoError(container.take_error());

  // Remove any memory region marked as reserved.
  for (const auto& range : container) {
    if (range.type != ZBI_MEM_RANGE_RESERVED) {
      continue;
    }
    zx::status<> result = allocator.RemoveRange(range.paddr, range.length);
    ZX_ASSERT(result.is_ok());
  }
  AssertNoError(container.take_error());

  // Remove our code from the range of useable memory.
  auto start = reinterpret_cast<uintptr_t>(&PHYS_LOAD_ADDRESS);
  auto end = reinterpret_cast<uintptr_t>(&_end);
  ZX_ASSERT(allocator.RemoveRange(start, /*size=*/end - start).is_ok());

  // Remove space occupied by the ZBI.
  zx::status<> result =
      allocator.RemoveRange(reinterpret_cast<uint64_t>(view.storage().data()), view.size_bytes());
  ZX_ASSERT(result.is_ok());

  // Remove the bottom page, to avoid confusion with nullptr.
  result = allocator.RemoveRange(0, ZX_PAGE_SIZE);
  ZX_ASSERT(result.is_ok());

  // Remove any arch-specific reserved ranges.
  ArchRemoveReservedRanges(allocator);
}

void* AllocateMemory(size_t size, size_t alignment) {
  zx::status<uint64_t> result = GetAllocator().Allocate(size, alignment);
  if (result.is_error()) {
    return nullptr;
  }
  return reinterpret_cast<void*>(result.value());
}

void FreeMemory(void* ptr, size_t size) {
  zx::status<> result =
      GetAllocator().AddRange(reinterpret_cast<uint64_t>(ptr), static_cast<uint64_t>(size));
  ZX_ASSERT(result.is_ok());
}
