// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/memalloc/allocator.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <ktl/string_view.h>
#include <phys/allocation.h>
#include <phys/main.h>
#include <phys/page-table.h>

namespace {

using ZbiView = zbitl::View<zbitl::ByteView>;

// Print all memory ranges in the given ZbiView.
void PrintMemoryRanges(const zbitl::MemRangeTable& table) {
  printf("Memory ranges present in ZBI:\n");
  for (const auto& range : table) {
    ktl::string_view name = zbitl::MemRangeTypeName(range.type);
    if (name.empty()) {
      name = "unknown";
    }
    printf("  paddr: [0x%16" PRIx64 " -- 0x%16" PRIx64 ") : size %10" PRIu64 " kiB : %.*s (%#x)\n",
           range.paddr, range.paddr + range.length, range.length / 1024,
           static_cast<int>(name.size()), name.data(), range.type);
  }
  printf("\n");
}

}  // namespace

void InitMemory(void* zbi) {
  zbitl::View<zbitl::ByteView> view{
      zbitl::StorageFromRawHeader(static_cast<const zbi_header_t*>(zbi))};

  // Find memory information.
  fitx::result<std::string_view, zbitl::MemRangeTable> memory =
      zbitl::MemRangeTable::FromView(view);
  if (memory.is_error()) {
    ZX_PANIC("Could not read system memory layout: %*s.\n",
             static_cast<int>(memory.error_value().size()), memory.error_value().data());
  }

  // Print memory information.
  PrintMemoryRanges(*memory);

  // Add all memory claimed to be free to the allocator.
  memalloc::Allocator& allocator = Allocation::GetAllocator();
  for (const auto& range : *memory) {
    // Ignore reserved memory on our first pass.
    if (range.type != ZBI_MEM_RANGE_RAM) {
      continue;
    }
    zx::status<> result = allocator.AddRange(range.paddr, range.length);
    ZX_ASSERT(result.is_ok());
  }

  // Remove any memory region marked as reserved.
  for (const auto& range : *memory) {
    if (range.type != ZBI_MEM_RANGE_RESERVED) {
      continue;
    }
    zx::status<> result = allocator.RemoveRange(range.paddr, range.length);
    ZX_ASSERT(result.is_ok());
  }

  // Remove space occupied by the ZBI.
  zx::status<> result =
      allocator.RemoveRange(reinterpret_cast<uint64_t>(view.storage().data()), view.size_bytes());
  ZX_ASSERT(result.is_ok());

  // Remove space occupied by the program itself.
  Allocation::InitReservedRanges();

  // Set up our own address space.
  ArchSetUpAddressSpaceEarly(*memory);
}
