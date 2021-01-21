// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/zbitl/items/mem_config.h>
#include <lib/zbitl/view.h>
#include <lib/zx/status.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/limits.h>

#include <fbl/algorithm.h>
#include <ktl/byte.h>
#include <ktl/span.h>

#include "../memory.h"
#include "test-main.h"

const char Symbolize::kProgramName_[] = "phys-memory-test";

namespace {

constexpr uint64_t kMiB [[maybe_unused]] = 1024 * 1024;

// Allocate and overwrite all RAM from the given memalloc::Allocator.
//
// Return the number of bytes that were in the allocator.
size_t AllocateAndOverwriteFreeMemory() {
  size_t bytes_allocated = 0;

  // To avoid having to call into the allocator too many times, we start
  // trying to do large allocations, and gradually ask for less and less
  // memory as the larger allocations fail.
  size_t allocation_size = kMiB;  // start with 1MiB allocations.
  while (allocation_size > 0) {
    // Allocate some memory.
    auto* result = reinterpret_cast<uint8_t*>(AllocateMemory(allocation_size));
    if (result == nullptr) {
      allocation_size /= 2;
      continue;
    }
    bytes_allocated += allocation_size;

    // Overwrite the memory.
    //
    // TODO(dgreenaway): We are currently running uncached on ARM64, which has
    // a memcpy throughput of ~5MiB/s (!). We only overwrite a small amount of
    // RAM to avoid the copy taking to long on systems with large amounts of RAM.
    constexpr size_t kMaxOverwrite = 64 * kMiB;
    if (bytes_allocated < kMaxOverwrite) {
      memset(result, 0x33, allocation_size);
    }
  }

  return bytes_allocated;
}

}  // namespace

int TestMain(void* zbi_ptr, arch::EarlyTicks ticks) {
  // Initialize memory for allocation/free.
  InitMemory(static_cast<const zbi_header_t*>(zbi_ptr));

  size_t bytes_allocated = AllocateAndOverwriteFreeMemory();
  if (bytes_allocated == 0) {
    printf("FAIL: Could not allocate any memory.\n");
    return 1;
  }

  return 0;
}
