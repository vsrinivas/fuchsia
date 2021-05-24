// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "legacy-boot.h"

#include <inttypes.h>
#include <lib/memalloc/allocator.h>
#include <zircon/assert.h>

#include <ktl/limits.h>
#include <phys/allocation.h>
#include <phys/symbolize.h>
#include <pretty/sizes.h>

void InitMemoryFromRanges() {
  char pretty_buffer[MAX_FORMAT_SIZE_LEN];
  auto pretty = [&pretty_buffer](uint64_t size) -> const char* {
    if (size < ktl::numeric_limits<size_t>::max()) {
      return format_size(pretty_buffer, sizeof(pretty_buffer), size);
    }
    if (size % (uint64_t{1} << 30) == 0) {
      snprintf(pretty_buffer, sizeof(pretty_buffer), "%" PRIu64 "G", size >> 30);
    } else {
      snprintf(pretty_buffer, sizeof(pretty_buffer), "%" PRIu64 "B", size);
    }
    return pretty_buffer;
  };

  auto& allocator = Allocation::GetAllocator();
  auto add_range = [&](uint64_t base, uint64_t size, auto what) {
    ZX_ASSERT_MSG(allocator.AddRange(base, size).is_ok(),
                  "Cannot add %s range [%#" PRIx64 ", %#" PRIx64 ")\n", what, base, size);
    printf("%s: [0x%016" PRIx64 ", 0x%016" PRIx64 ")  %12s %s added\n", Symbolize::kProgramName_,
           base, base + size, pretty(size), what);
  };
  auto remove_range = [&](uint64_t base, uint64_t size, auto what) {
    ZX_ASSERT_MSG(allocator.RemoveRange(base, size).is_ok(),
                  "Cannot remove %s range [%#" PRIx64 ", %#" PRIx64 ")\n", what, base, size);
    printf("%s: [0x%016" PRIx64 ", 0x%016" PRIx64 "): %12s %s removed\n", Symbolize::kProgramName_,
           base, base + size, pretty(size), what);
  };

  // Add normal memory first.
  for (const zbi_mem_range_t& range : gLegacyBoot.mem_config) {
    if (range.type == ZBI_MEM_RANGE_RAM) {
      add_range(range.paddr, range.length, "RAM");
    }
  }

  // Now remove everything else, in case it overlapped.
  for (const zbi_mem_range_t& range : gLegacyBoot.mem_config) {
    if (range.type != ZBI_MEM_RANGE_RAM) {
      remove_range(range.paddr, range.length, "reserved");
    }
  }

  auto reserve_string = [&](ktl::string_view s, const char* what) {
    if (!s.empty()) {
      remove_range(reinterpret_cast<uintptr_t>(s.data()), s.size() + 1, what);
    }
  };

  auto reserve_blob = [&](auto blob, const char* what) {
    if (!blob.empty()) {
      remove_range(reinterpret_cast<uintptr_t>(blob.data()), blob.size_bytes(), what);
    }
  };

  // Remove the memory occupied by the boot loader name and command line
  // strings present.  They will be copied into the data ZBI later, but that
  // requires allocation first.
  reserve_string(gLegacyBoot.bootloader, "boot loader name");
  reserve_string(gLegacyBoot.cmdline, "kernel command line");

  // Reserve the memory occupied by the RAMDISK (ZBI) image.
  reserve_blob(gLegacyBoot.ramdisk, "ZBI");

  // Reserve the memory occupied by the mem_config table itself.
  reserve_blob(gLegacyBoot.mem_config, "ZBI_TYPE_MEM_CONFIG table");

  // Remove space occupied by the program itself.
  Allocation::InitReservedRanges();

  if constexpr (sizeof(uintptr_t) < sizeof(uint64_t)) {
    // Remove everything above the part of the address space we can use.
    constexpr uint64_t ptr_max = ktl::numeric_limits<uintptr_t>::max();
    constexpr uint64_t start = ptr_max + 1;
    constexpr uint64_t size = ktl::numeric_limits<uint64_t>::max() - start + 1;
    remove_range(start, size, "unreachable address space");
  }
}
