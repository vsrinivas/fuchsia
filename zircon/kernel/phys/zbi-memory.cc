// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/memalloc.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/string_view.h>

#include "allocation.h"
#include "arch.h"
#include "main.h"

namespace {

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

// Print all memory ranges in the given ZbiView.
void PrintMemoryRanges(const ZbiView& view) {
  zbitl::MemRangeTable container{view};
  printf("Memory ranges present in ZBI:\n");
  for (const auto& range : container) {
    ktl::string_view name = zbitl::MemRangeTypeName(range.type);
    if (name.empty()) {
      name = "unknown";
    }
    printf("  paddr: [0x%16" PRIx64 " -- 0x%16" PRIx64 ") : size %10" PRIu64 " kiB : %.*s (%#x)\n",
           range.paddr, range.paddr + range.length, range.length / 1024,
           static_cast<int>(name.size()), name.data(), range.type);
  }
  printf("\n");
  AssertNoError(container.take_error());
}

}  // namespace

void InitMemory(void* zbi) {
  zbitl::View<zbitl::ByteView> view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi))};

  // Print memory information.
  PrintMemoryRanges(view);

  // Add all memory claimed to be free to the allocator.
  memalloc::Allocator& allocator = Allocation::GetAllocator();
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

  // Remove space occupied by the ZBI.
  zx::status<> result =
      allocator.RemoveRange(reinterpret_cast<uint64_t>(view.storage().data()), view.size_bytes());
  ZX_ASSERT(result.is_ok());

  // Remove space occupied by the program itself.
  Allocation::InitReservedRanges();

  // Set up our own address space.
  ArchSetUpAddressSpace(Allocation::GetAllocator(), container);
}
