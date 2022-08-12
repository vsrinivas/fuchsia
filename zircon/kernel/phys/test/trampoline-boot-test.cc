// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <inttypes.h>
#include <lib/arch/random.h>
#include <lib/arch/zbi-boot.h>
#include <lib/boot-options/boot-options.h>
#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/zbitl/error-stdio.h>
#include <lib/zbitl/item.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string-file.h>
#include <zircon/assert.h>
#include <zircon/boot/image.h>

#include <fbl/alloc_checker.h>
#include <ktl/algorithm.h>
#include <ktl/array.h>
#include <ktl/iterator.h>
#include <ktl/limits.h>
#include <ktl/string_view.h>
#include <ktl/unique_ptr.h>
#include <phys/allocation.h>
#include <phys/boot-zbi.h>
#include <phys/main.h>
#include <phys/new.h>
#include <phys/stdio.h>
#include <phys/symbolize.h>
#include <phys/trampoline-boot.h>
#include <pretty/cpp/sizes.h>

#include "turducken.h"

#include <ktl/enforce.h>

#define span_arg(s) static_cast<int>(s.size()), reinterpret_cast<const char*>(s.data())

// Declared in turducken.h.
const char* kTestName = "trampoline-boot-test";

namespace {
// User argument for setting the seed to use in the first iteration.
constexpr ktl::string_view kUserSeedOpt = "trampoline.user_seed=";

// User argument for setting the number of iterations to perform.
constexpr ktl::string_view kUserTotalIterationsOpt = "trampoline.user_total_iters=";

// Internal arguments for communicating state throughout each iteration, for validation purposes.

// Used to communicate to the next kernel item what the seed to use is.
constexpr ktl::string_view kSeedOpt = "trampoline.state.seed=";

// Used to communicate to the next kernel item what the expected load address is.
// If provided, will fix the value to a specific load address.
constexpr ktl::string_view kKernelLoadAddressOpt = "trampoline.state.kernel_load_address=";

// Used to communicate to the next kernel item what the expected address of the data ZBI is.
// If provided, will fix the the value to a specific load address.
constexpr ktl::string_view kDataLoadAddressOpt = "trampoline.state.data_load_address=";

// Keeps track of the total number of iterations to perform.
// If not set, will default to one.
constexpr ktl::string_view kRemainingIterationsOpt = "trampoline.state.remaining_iterations=";

// This is used as a marker to notify that user arguments have been parsed,
// and that trampoline state is present in the last command line item.
// If not set, will default to false.
constexpr ktl::string_view kIsReadyOpt = "trampoline.state.ready=";

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
// Coalesce all allocatable ranges, that is any non reserved or peripheral range.
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

uint64_t GetMemoryAddress(BootZbi::Size size, uint64_t& seed) {
  uint64_t address = GetRandomAlignedMemoryRange(Allocation::GetPool(), size, seed);
  ZX_ASSERT_MSG(address % size.alignment == 0,
                "memory address(0x%016" PRIx64 ") is not aligned at boundary(0x%016" PRIx64 ")",
                address, size.alignment);
  return address;
}

constexpr uint64_t HexOptionSize(ktl::string_view name) {
  // 'opt=0x0000000 '
  return name.length() + 16 + 2;
}

constexpr uint32_t GetCommandLinePayloadLength() {
  // opt=value opt2=value2 ....
  constexpr size_t cmdline_payload_length =
      HexOptionSize(kKernelLoadAddressOpt) + HexOptionSize(kDataLoadAddressOpt) +
      HexOptionSize(kRemainingIterationsOpt) + HexOptionSize(kSeedOpt) + kIsReadyOpt.length() +
      ktl::string_view("true").length() + 4;

  constexpr uint32_t length = static_cast<uint32_t>(cmdline_payload_length);
  static_assert(length == cmdline_payload_length);
  return length;
}

constexpr uint64_t GetCommandLineItemLength() {
  // Aligned zbi_header | payload.
  return sizeof(zbi_header_t) + ZBI_ALIGN(GetCommandLinePayloadLength());
}

void UpdateCommandLineZbiItem(uint64_t kernel_load_address, uint64_t data_load_address,
                              uint64_t seed, uint64_t iteration, ktl::span<ktl::byte> payload) {
  // Add an extra imaginary byte, so we dont need to reserve for a nul byte.
  // Calling |take| here would cause a segfault, it is ok for payload not to be nul terminated.
  StringFile writer({reinterpret_cast<char*>(payload.data()), payload.size_bytes() + 1});

  auto append_kv = [&writer](ktl::string_view option, uint64_t value) {
    writer.Write(option);
    fprintf(&writer, "0x%016" PRIx64 " ", value);
  };

  append_kv(kKernelLoadAddressOpt, kernel_load_address);
  append_kv(kDataLoadAddressOpt, data_load_address);
  append_kv(kSeedOpt, seed);
  append_kv(kRemainingIterationsOpt, iteration);
  writer.Write(kIsReadyOpt);
  writer.Write("true");

  // Payload takes into account the null terminator while the written bytes do not.
  ZX_ASSERT_MSG(writer.used_region().size() == GetCommandLinePayloadLength(),
                "written_bytes %zu payload_length %" PRIu32, writer.used_region().size(),
                GetCommandLinePayloadLength());
}

uint64_t GetOptionOrDefault(ktl::string_view option_name,
                            ktl::optional<ktl::string_view> maybe_option, uint64_t default_value) {
  if (maybe_option) {
    auto maybe_value = TurduckenTestBase::ParseUint(*maybe_option);
    ZX_ASSERT_MSG(maybe_value, "%*.s is invalid value for %*.s\n", span_arg(maybe_option.value()),
                  span_arg(option_name));
    return *maybe_value;
  }
  return default_value;
}

uint64_t ParseHex(ktl::optional<ktl::string_view> maybe_opt) {
  ZX_ASSERT(maybe_opt);
  ZX_ASSERT(maybe_opt->length() <= 18);
  ktl::array<char, 19> hex_str = {};
  memcpy(hex_str.data(), maybe_opt->data(), maybe_opt->length());
  return strtoul(hex_str.data(), nullptr, 16);
}

template <typename Result>
void CheckError(Result res) {
  if (res.is_error()) {
    zbitl::PrintViewError(res.error_value());
    ZX_PANIC("Encountered Error.");
  }
}

template <typename Zbi>
uint64_t GetRandomSeed(Zbi& zbi) {
  if (arch::Random<true>::Supported()) {
    while (true) {
      if (auto seed = arch::Random<true>::Get()) {
        return *seed;
      }
    }
  }

  if (arch::Random<false>::Supported()) {
    while (true) {
      if (auto seed = arch::Random<false>::Get()) {
        return *seed;
      }
    }
  }

  // Then there must be entropy item.
  for (auto [h, p] : zbi) {
    if (h->type == ZBI_TYPE_SECURE_ENTROPY && p.size() >= sizeof(uint64_t)) {
      uint64_t seed = 0;
      memcpy(&seed, reinterpret_cast<const void*>(p.data()), sizeof(seed));
      zbi.ignore_error();
      return seed;
    }
  }

  // Or through the cmdline.
  if (gBootOptions->entropy_mixin.len > 0) {
    return ParseHex(ktl::string_view{gBootOptions->entropy_mixin.c_str(),
                                     ktl::min<size_t>(16, gBootOptions->entropy_mixin.len)});
  }

  ZX_PANIC("No source of entropy available.");
}

// Allows for loading a zbi that skips certain items, this allows for faster iteration by
// intentionally discarding big items that have no purpose for this test.
template <typename ViewIt, typename FilterFn>
void LoadWithFilter(TurduckenTest& turducken, ViewIt kernel_it, ViewIt first, ViewIt last,
                    size_t extra_capacity, FilterFn filter) {
  auto& view = first.view();
  const size_t last_offset = last == view.end() ? view.size_bytes() : last.item_offset();
  const size_t extra = last_offset - first.item_offset() + extra_capacity;

  // Loads just the decompressed kernel for next boot and allocate enough space for the data zbi.
  turducken.Load(kernel_it, first, first, static_cast<uint32_t>(extra));
  auto zbi = turducken.loaded_zbi();

  auto range_end = first;
  auto range_start = first;

  // Append ranges, by skipping any |filter(it)| that returns true.
  while (range_start != last && range_start != view.end()) {
    // Find starting point.
    while (range_start != last && range_start != view.end() && filter(range_start)) {
      range_start++;
    }

    // Find end of the range.
    range_end = range_start;
    while (range_end != last && range_end != view.end() && !filter(range_end)) {
      range_end++;
    }

    auto extend_result = zbi.Extend(range_start, range_end);
    if (extend_result.is_error()) {
      printf("%s: failed to extend embedded ZBI: ", ProgramName());
      zbitl::PrintViewCopyError(extend_result.error_value());
      printf("\n");
      abort();
    }
    range_start = range_end;
  }
}

// Filters device tree items from an input ZBI.
// The device tree item size is 1 MiB, while the rest of the payload is on the ~80 KiB mark.
// This item dramatically reduces the number of iterations we can perform(~4x app).
auto device_tree_filter = [](auto it) {
  ZX_ASSERT(it != it.view().end());
  return it->header->type == ZBI_TYPE_DEVICETREE;
};

}  // namespace

int TurduckenTest::Main(Zbi::iterator kernel_item) {
  uint64_t seed = 0;
  uint64_t remaining_iterations = 0;
  uint32_t extra_capacity = 0;

  bool is_ready = OptionWithPrefix(kIsReadyOpt).has_value();
  debugf("%s: is_ready: %s\n", test_name(), is_ready ? "true" : "false");
  auto total_iterations =
      GetOptionOrDefault(kUserTotalIterationsOpt, OptionWithPrefix(kUserTotalIterationsOpt), 1);
  if (!is_ready) {
    remaining_iterations = total_iterations;
    seed =
        GetOptionOrDefault(kUserSeedOpt, OptionWithPrefix(kUserSeedOpt), GetRandomSeed(boot_zbi()));

    // The loaded ZBI needs to account for a command line item that will contain the propagated
    // state between iterations.
    extra_capacity = GetCommandLineItemLength();
  } else {
    // This is non-bootstrap iteration, and we need to load the state and validate invariants.
    seed = ParseHex(OptionWithPrefix(kSeedOpt));
    remaining_iterations = ParseHex(OptionWithPrefix(kRemainingIterationsOpt)) - 1;

    auto check = [this](ktl::string_view option, uint64_t actual) {
      auto expected = ParseHex(OptionWithPrefix(option));
      ZX_ASSERT_MSG(actual == expected,
                    "%.*s (0x%016" PRIx64 ") != expected_load_address (0x%016" PRIx64 ")",
                    span_arg(option), actual, expected);
    };
    check(kKernelLoadAddressOpt, reinterpret_cast<uintptr_t>(PHYS_LOAD_ADDRESS));
    check(kDataLoadAddressOpt, reinterpret_cast<uintptr_t>(boot_zbi().storage().data()));
  }

  debugf("%s: random_seed: %" PRIu64 "\n", test_name(), seed);
  debugf("%s: remaining_iterations: %" PRIu64 "\n", test_name(), remaining_iterations);
  debugf("%s: total_iterations: %" PRIu64 "\n", test_name(), total_iterations);

  if (remaining_iterations == 0) {
    debugf("%s: All iterations completed.\n", test_name());
    return 0;
  }

  // Remove any unwanted items from the loaded zbi on bootstrap.
  if (!is_ready) {
    LoadWithFilter(*this, kernel_item, kernel_item, boot_zbi().end(), extra_capacity,
                   device_tree_filter);
  } else {
    Load(kernel_item, kernel_item, boot_zbi().end(), extra_capacity);
  }

  ktl::span<ktl::byte> cmdline_item_payload;

  if (!is_ready) {
    // We've allocated enough space for this command line item, now we can just extend it.
    uint32_t payload_length = GetCommandLinePayloadLength();
    auto it_or = loaded_zbi().Append({.type = ZBI_TYPE_CMDLINE, .length = payload_length});
    CheckError(it_or);
    cmdline_item_payload = it_or->payload;
  } else {
    // Find the last item from the command line item, whose payload length matches the
    // state payload length.
    auto zbi = loaded_zbi();
    for (auto& [h, payload] : zbi) {
      if (h->type == ZBI_TYPE_CMDLINE && payload.size() == GetCommandLinePayloadLength()) {
        cmdline_item_payload = payload;
      }
    }
    ZX_ASSERT(!cmdline_item_payload.empty());
    CheckError(zbi.take_error());
  }

  // Pick random valid memory ranges.
  uint64_t kernel_load_address =
      GetMemoryAddress(BootZbi::GetKernelAllocationSize(kernel_item), seed);
  uint64_t data_load_address =
      GetMemoryAddress(BootZbi::Size{.size = loaded_zbi().storage().size(),
                                     .alignment = arch::kZbiBootDataAlignment},
                       seed);

  set_kernel_load_address(kernel_load_address);
  set_data_load_address(data_load_address);

  debugf("%s: kernel_load_address: 0x%016" PRIx64 "\n", test_name(), kernel_load_address);
  debugf("%s: data_load_address: 0x%016" PRIx64 "\n", test_name(), data_load_address);

  // Overwrite the state payload with the updated state.
  UpdateCommandLineZbiItem(kernel_load_address, data_load_address, seed, remaining_iterations,
                           cmdline_item_payload);
  Boot();
  /*NOTREACHED*/
}
