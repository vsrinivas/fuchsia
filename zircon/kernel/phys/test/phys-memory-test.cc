// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/memalloc.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/limits.h>

#include <fbl/algorithm.h>
#include <ktl/byte.h>
#include <ktl/span.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "phys-memory-test";

namespace {

constexpr uint64_t kMiB [[maybe_unused]] = 1024 * 1024;
constexpr uint64_t kGiB [[maybe_unused]] = 1024 * 1024 * 1024;

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

// Allocate and overwrite all RAM from the given memalloc::Allocator.
//
// Return the number of bytes that were in the allocator.
uint64_t AllocateAndOverwriteFreeMemory(memalloc::Allocator* allocator) {
  uint64_t bytes_allocated = 0;

  // To avoid having to call into the allocator too many times, we start
  // trying to do large allocations, and gradually ask for less and less
  // memory as the larger allocations fail.
  uint64_t allocation_size = kMiB;  // start with 1MiB allocations.
  while (allocation_size > 0) {
    // Allocate some memory.
    zx::status<uint64_t> result = allocator->Allocate(allocation_size);
    if (result.is_error()) {
      allocation_size /= 2;
      continue;
    }
    bytes_allocated += allocation_size;

    // Overwrite the memory.
    //
    // TODO(dgreenaway): We are currently running uncached on ARM64, which has
    // a memcpy throughput of ~5MiB/s (!). We only overwrite a small amount of
    // RAM to avoid the copy taking to long on systems with large amounts of RAM.
    constexpr uint64_t kMaxOverwrite = 64 * kMiB;
    auto* bytes = reinterpret_cast<std::byte*>(result.value());
    if (bytes_allocated < kMaxOverwrite) {
      memset(bytes, 0x33, static_cast<size_t>(allocation_size));
    }
  }

  return bytes_allocated;
}

// Remove architecture-specific regions of memory.
void ArchRemoveReservedRanges(memalloc::Allocator* allocator) {
#if defined(__x86_64__)
  // On x86, remove space likely to be holding our page tables.
  //
  // TODO(dgreenaway): We assume here that the page tables are contiguously
  // allocated, starting at CR3, and all fitting within 1MiB. We should remove
  // these assumptions.
  {
    // Get top-level page directory location, stored in the CR3 register.
    uint64_t cr3;
    __asm__("mov %%cr3, %0\n\t" : "=r"(cr3));

    // Remove the range.
    zx::status<> result = allocator->RemoveRange(cr3, 1 * kMiB);
    ZX_ASSERT(result.is_ok());
  }

  // On x86-64, remove space unlikely to be mapped into our address space (anything past 1 GiB).
  zx::status<> result = allocator->RemoveRange(1 * kGiB, UINT64_MAX - 1 * kGiB + 1);
  ZX_ASSERT(result.is_ok());
#endif
}

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks ticks) {
  // Skip tests on systems that don't use ZBI, such as QEMU.
  //
  // In future, we will want to use alternative mechanisms to locate
  // memory in such platforms.
  if (zbi_ptr == nullptr) {
    printf("No ZBI found. Skipping test...\n");
    return 0;
  }
  zbitl::View<zbitl::ByteView> view({static_cast<ktl::byte*>(zbi_ptr), SIZE_MAX});

  // Print memory information.
  zbitl::MemRangeTable container{view};
  printf("Memory ranges detected:\n");
  size_t count = 0;
  for (const auto& range : container) {
    printf("  paddr: [0x%16" PRIx64 " -- 0x%16" PRIx64 ") : size %10" PRIu64 " kiB : %s\n",
           range.paddr, range.paddr + range.length, range.length / 1024,
           RangeTypeString(range.type));
    count++;
  }
  printf("\n");

  // Check for errors during iteration.
  if (auto result = container.take_error(); result.is_error()) {
    printf("Error while scanning memory ranges: %.*s\n",
           static_cast<int>(result.error_value().zbi_error.size()),
           result.error_value().zbi_error.data());
    return 1;
  }

  // Ensure we found at least one range.
  if (count == 0) {
    printf("Error: no memory ranges found.\n");
    return 1;
  }

  // Add all memory claimed to be free to the allocator.
  constexpr size_t kMaxRanges = 32;
  memalloc::Range ranges[kMaxRanges];
  static_assert(sizeof(ranges) <= 1024, "`ranges` too large for stack.");
  memalloc::Allocator allocator(ranges);
  for (const auto& range : container) {
    // Ignore reserved memory on our first pass.
    if (range.type != ZBI_MEM_RANGE_RAM) {
      continue;
    }
    zx::status<> result = allocator.AddRange(range.paddr, range.length);
    ZX_ASSERT(result.is_ok());
  }
  ZX_ASSERT(container.take_error().is_ok());

  // Remove any memory region marked as reserved.
  for (const auto& range : container) {
    if (range.type != ZBI_MEM_RANGE_RESERVED) {
      continue;
    }
    zx::status<> result = allocator.RemoveRange(range.paddr, range.length);
    ZX_ASSERT(result.is_ok());
  }
  ZX_ASSERT(container.take_error().is_ok());

  // Remove our code from the range of useable memory.
  auto start = reinterpret_cast<uint64_t>(&PHYS_LOAD_ADDRESS);
  auto end = reinterpret_cast<uint64_t>(&_end);
  ZX_ASSERT(allocator.RemoveRange(start, /*size=*/end - start).is_ok());

  // Remove space occupied by the ZBI.
  zx::status<> result =
      allocator.RemoveRange(reinterpret_cast<uint64_t>(view.storage().data()), view.size_bytes());
  ZX_ASSERT(result.is_ok());

  // Remove any arch-specific reserved ranges.
  ArchRemoveReservedRanges(&allocator);

  // Remove the zero byte, to avoid confusion with nullptr.
  result = allocator.RemoveRange(0, 1);
  ZX_ASSERT(result.is_ok());

  // Ensure we can allocate all the reamining RAM and overwrite it.
  uint64_t bytes_allocated = AllocateAndOverwriteFreeMemory(&allocator);

  // Print the number of bytes allocated, and ensure we found at least 1 byte of free memory.
  printf("Detected %10" PRIu64 " kiB of free memory.",
         static_cast<uint64_t>(bytes_allocated / 1024));
  if (bytes_allocated == 0) {
    return 1;
  }

  return 0;
}
