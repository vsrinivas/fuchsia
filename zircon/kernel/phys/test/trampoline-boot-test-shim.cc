// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/arch/zbi-boot.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/item.h>
#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>
#include <zircon/boot/multiboot.h>

#include <cstdio>
#include <cstdlib>
#include <limits>

#include <fbl/alloc_checker.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>
#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/new.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>
#include <phys/trampoline-boot.h>
#include <pretty/cpp/sizes.h>

#include "turducken.h"

#include <ktl/enforce.h>

#define span_arg(s) static_cast<int>(s.size()), reinterpret_cast<const char*>(s.data())

// Declared in turducken.h.
const char* kTestName = "trampoline-boot-shim-test";

namespace {

// When set, dictates random decisions done by the trampoline boot test.
constexpr ktl::string_view kSeedOpt = "trampoline.seed=";

// Used to communicate to the next kernel item what the expected load address is.
// If provided, will fix the value to a specific load address.
constexpr ktl::string_view kKernelLoadAddressOpt = "trampoline.kernel_load_address=";

// Used to communicate to the next kernel item what the expected address of the data ZBI is.
// If provided, will fix the the value to a specific load address.
constexpr ktl::string_view kDataLoadAddressOpt = "trampoline.data_load_address=";

struct RangeCollection {
  RangeCollection() = delete;
  explicit RangeCollection(size_t capacity) : capacity(capacity) {
    fbl::AllocChecker ac;
    data = new (gPhysNew<memalloc::Type::kPhysScratch>, ac) memalloc::Range[capacity];
    ZX_ASSERT(ac.check());
  }

  auto view() const { return ktl::span<memalloc::Range>(data, size); }

  memalloc::Range* data;
  size_t size = 0;
  size_t capacity;
};

// Returns a range collection, with all the non special ranges, where memory can be allocated from.
// This ranges in this collection don't care about the specific type, just whether they where
// reserved by the bootloader for some reason.
RangeCollection FindAllocableRanges(memalloc::Pool& pool) {
  // Collection of ranges where memory can be allocated from. Ranges in this collection, are not
  // necessarily valid for the kernel,since it might not fit.
  RangeCollection ranges(pool.size());

  memalloc::Range* prev = nullptr;

  for (auto& range : pool) {
    // Special ranges.
    if (range.type == memalloc::Type::kReserved || range.type == memalloc::Type::kPeripheral) {
      continue;
    }

    // Skip allocations for the payloads of this test.
    if (range.type == memalloc::Type::kZbiTestPayload) {
      continue;
    }

    // Disjoint range.
    if (prev == nullptr || prev->end() != range.addr) {
      ranges.size++;
      ranges.view().back() = range;
      prev = &ranges.view().back();
      // Remove address 0 since is source of problems. By adding 1 offset, the alignment will take
      // care of the rest.
      if (prev->addr == 0) {
        prev->addr++;
      }
      continue;
    }

    // Coalescing range.
    prev->size += range.size;
  }

  ZX_ASSERT(!ranges.view().empty());
  return ranges;
}

RangeCollection FindCandidateRanges(const RangeCollection& allocable_ranges, size_t size,
                                    size_t alignment) {
  // Each candidate range represents a valid starting point, and a wiggle room, that is,
  // how many bytes can an allocation be shifted.
  RangeCollection ranges(allocable_ranges.size);
  for (auto range : allocable_ranges.view()) {
    if (range.size < size) {
      continue;
    }

    // No valid aligned address withing the range.
    if (range.end() - (alignment - 1) < range.addr) {
      continue;
    }
    size_t aligned_addr = (range.addr + alignment - 1) & ~(alignment - 1);

    size_t size_slack = range.size - size;
    size_t unaligned_bytes = (aligned_addr - range.addr);
    if (size_slack < unaligned_bytes) {
      continue;
    }

    // At this point a valid candidate for allocating a contiguous range for the kernel has been
    // found. Any existing allocations are not important, since this is looking for the final
    // location where the trampoline boot will load stuff into.
    ranges.size++;
    auto& candidate_range = ranges.view().back();
    candidate_range.addr = aligned_addr;
    candidate_range.size = size_slack - unaligned_bytes;
  }
  ZX_ASSERT(!ranges.view().empty());
  return ranges;
}

// Pick an allocation range from available ranges in the |memalloc::Pool|.
// Coalesce all allocatable ranges, that is, any non null region, reserved or peripheral range.
uint64_t GetRandomAlignedMemoryRange(memalloc::Pool& pool, BootZbi::Size size, uint64_t& seed) {
  // Each candidate range represents a valid starting point, and a wiggle room, that is,
  // how many bytes can an allocation be shifted.
  auto allocable_ranges = FindAllocableRanges(pool);
  auto candidate_ranges = FindCandidateRanges(allocable_ranges, size.size, size.alignment);

  // Now we randomly pick a valid candidate range.
  uint64_t range_index = rand_r(&seed) % candidate_ranges.size;
  auto& selected_range = candidate_ranges.view()[range_index];

  // Each candidate range is represented as:
  //     addr -> aligned address where the allocation fits.
  //     size -> extra bytes at the tail of the possible allocation starting at addr.
  uint64_t target_address = selected_range.addr;
  uint64_t aligned_slots = selected_range.size / size.alignment;
  if (aligned_slots > 0) {
    uint64_t selected_slot = rand_r(&seed) % aligned_slots;
    target_address += selected_slot * size.alignment;
  }

  ZX_ASSERT_MSG(
      pool.UpdateFreeRamSubranges(memalloc::Type::kZbiTestPayload, target_address, size.size)
          .is_ok(),
      "Insufficient bookkeeping to track new ranges.");
  return target_address;
}

}  // namespace

int TurduckenTest::Main(Zbi::iterator kernel_item) {
  auto seed_opt = OptionWithPrefix(kSeedOpt);
  uint64_t kernel_load_address = 0;
  uint64_t data_load_address = 0;
  uint64_t seed = 0;

  if (seed_opt) {
    if (auto seed_val = ParseUint(seed_opt.value())) {
      seed = *seed_val;
    }
  } else {
    seed = rand_r(&seed);
    debugf("%s: random_seed: %" PRIu64 "\n", test_name(), seed);
  }

  // Accommodate for snprintf writing a null terminator, instead of truncating the string.
  // Hex string: 16
  // 0x prefix: 2
  // NUL termination: 1
  // whitespace option separation: 1
  uint32_t load_address_str_length =
      kKernelLoadAddressOpt.length() + kDataLoadAddressOpt.length() + 2 * (16 + 2) + 1 + 1;
  uint32_t cmdline_item_length =
      ZBI_ALIGN(static_cast<uint32_t>(sizeof(zbi_header_t)) + load_address_str_length);
  Load(kernel_item, kernel_item, boot_zbi().end(), cmdline_item_length);

  if (auto kernel_load_addr_opt = OptionWithPrefix(kKernelLoadAddressOpt)) {
    auto maybe_load_addr = pretty::ParseSizeBytes(kernel_load_addr_opt.value());
    ZX_ASSERT_MSG(maybe_load_addr, "%.*s contains invalid value %.*s",
                  span_arg(kKernelLoadAddressOpt), span_arg(kernel_load_addr_opt.value()));
    kernel_load_address = *maybe_load_addr;
  } else {
    auto alloc = BootZbi::GetKernelAllocationSize(kernel_item);
    kernel_load_address = GetRandomAlignedMemoryRange(Allocation::GetPool(), alloc, seed);
  }
  ZX_ASSERT_MSG(kernel_load_address % arch::kZbiBootKernelAlignment == 0,
                "kernel_load_address(0x%016" PRIx64 ") must be aligned(0x%016" PRIx64 ")",
                kernel_load_address, arch::kZbiBootKernelAlignment);
  debugf("%s: kernel_load_address: 0x%016" PRIx64 "\n", test_name(), kernel_load_address);
  set_kernel_load_address(kernel_load_address);

  if (auto data_load_addr_opt = OptionWithPrefix(kDataLoadAddressOpt)) {
    auto maybe_load_addr = pretty::ParseSizeBytes(data_load_addr_opt.value());
    ZX_ASSERT_MSG(maybe_load_addr, "%.*s contains invalid value %.*s",
                  span_arg(kDataLoadAddressOpt), span_arg(data_load_addr_opt.value()));
    data_load_address = *maybe_load_addr;
  } else {
    auto alloc = BootZbi::Size{.size = loaded_zbi().storage().size_bytes(),
                               .alignment = arch::kZbiBootDataAlignment};
    data_load_address = GetRandomAlignedMemoryRange(Allocation::GetPool(), alloc, seed);
  }
  ZX_ASSERT_MSG(data_load_address % arch::kZbiBootDataAlignment == 0,
                "data_load_address(0x%016" PRIx64 ") must be aligned(0x%016" PRIx64 ")",
                data_load_address, arch::kZbiBootDataAlignment);
  debugf("%s: data_load_address: 0x%016" PRIx64 "\n", test_name(), kernel_load_address);
  set_data_load_address(data_load_address);

  // Append the new option.
  auto it_or = loaded_zbi().Append(
      {.type = ZBI_TYPE_CMDLINE, .length = static_cast<uint32_t>(load_address_str_length)});
  ZX_ASSERT(it_or.is_ok());
  auto buffer = it_or->payload;
  uint32_t written_bytes =
      snprintf(reinterpret_cast<char*>(buffer.data()), buffer.size(),
               "%.*s0x%016" PRIx64 " %.*s0x%016" PRIx64, span_arg(kKernelLoadAddressOpt),
               kernel_load_address, span_arg(kDataLoadAddressOpt), data_load_address);
  // Remove the extra char to accommodate the added null terminator.
  ZX_ASSERT_MSG(written_bytes == load_address_str_length - 1,
                "written_bytes %" PRIu32 " load_address_str_length %" PRIu32, written_bytes,
                load_address_str_length);
  Boot();
  /*NOTREACHED*/
}
