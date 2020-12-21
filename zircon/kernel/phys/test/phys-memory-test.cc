// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/zbitl/items/mem_config.h>
#include <stdio.h>
#include <string.h>
#include <zircon/assert.h>

#include <ktl/byte.h>
#include <ktl/span.h>

#include "test-main.h"

const char Symbolize::kProgramName_[] = "phys-memory-test";

namespace {

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

  // Print memory information.
  zbitl::MemRangeTable container{
      zbitl::View<zbitl::ByteView>({static_cast<ktl::byte*>(zbi_ptr), ktl::dynamic_extent})};
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

  return 0;
}
