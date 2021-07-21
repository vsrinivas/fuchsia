// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <zircon/assert.h>

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "test.h"

namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

enum class Action {
  kAllocate,
  kUpdateFreeRamSubranges,
  kFree,
  kMaxValue,  // Required by FuzzedDataProvider::ConsumeEnum().
};

struct Allocation {
  uint64_t addr, size;

  constexpr uint64_t end() const { return addr + size; }
};

bool IsValidPoolInitInput(cpp20::span<memalloc::MemRange> ranges) {
  // The valid input spaces of Pool::Init() and FindNormalizedRanges()
  // coincide. Since the latter returns an error, we use that as a proxy to
  // vet inputs to the former (taking that it works as expected for granted).
  constexpr auto noop = [](const memalloc::MemRange& range) { return true; };
  const size_t scratch_size = memalloc::FindNormalizedRangesScratchSize(ranges.size());
  auto scratch = std::make_unique<void*[]>(scratch_size);
  return memalloc::FindNormalizedRanges({ranges}, {scratch.get(), scratch_size}, noop).is_ok();
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  size_t num_range_bytes = provider.ConsumeIntegralInRange<size_t>(0, provider.remaining_bytes());
  std::vector<std::byte> bytes = provider.ConsumeBytes<std::byte>(num_range_bytes);
  cpp20::span<memalloc::MemRange> ranges = RangesFromBytes(bytes);

  if (!IsValidPoolInitInput(ranges)) {
    return 0;
  }

  PoolContext ctx;
  if (auto result = ctx.pool.Init(std::array{ranges}); result.is_error()) {
    return 0;
  }

  ZX_ASSERT_MSG(std::is_sorted(ctx.pool.begin(), ctx.pool.end()),
                "pool ranges are not sorted:\n%s\noriginal ranges:\n%s",
                ToString(ctx.pool.begin(), ctx.pool.end()).c_str(), ToString(ranges).c_str());

  // Tracks the non-bookkeeping allocations made that have yet to be partially
  // freed; this will serve as a means of generating valid inputs to Free().
  std::vector<Allocation> allocations;

  while (provider.remaining_bytes()) {
    switch (provider.ConsumeEnum<Action>()) {
      case Action::kAllocate: {
        auto type = static_cast<memalloc::Type>(provider.ConsumeIntegralInRange<uint64_t>(
            memalloc::kMinExtendedTypeValue, memalloc::kMaxExtendedTypeValue));
        uint64_t size = provider.ConsumeIntegralInRange<uint64_t>(1, kMax);
        uint64_t alignment = uint64_t{1} << provider.ConsumeIntegralInRange<size_t>(0, 63);
        uint64_t max_addr = provider.ConsumeIntegral<uint64_t>();
        if (auto result = ctx.pool.Allocate(type, size, alignment, max_addr); result.is_ok()) {
          // We cannot free Free() bookkeeping ranges.
          if (type != memalloc::Type::kPoolBookkeeping) {
            allocations.emplace_back(Allocation{.addr = std::move(result).value(), .size = size});
          }
        }
        break;
      }
      case Action::kUpdateFreeRamSubranges: {
        auto type = static_cast<memalloc::Type>(provider.ConsumeIntegralInRange<uint64_t>(
            memalloc::kMinExtendedTypeValue, memalloc::kMaxExtendedTypeValue));
        uint64_t addr = provider.ConsumeIntegral<uint64_t>();
        uint64_t size = provider.ConsumeIntegralInRange<uint64_t>(0, kMax - addr);
        (void)ctx.pool.UpdateFreeRamSubranges(type, addr, size);
        break;
      }
      case Action::kFree: {
        if (allocations.empty()) {
          break;
        }

        // Pick a subrange of the last allocation.
        const auto& allocation = allocations.back();
        uint64_t addr =
            provider.ConsumeIntegralInRange<uint64_t>(allocation.addr, allocation.end());
        uint64_t size = provider.ConsumeIntegralInRange<uint64_t>(0, allocation.end() - addr);
        allocations.pop_back();
        (void)ctx.pool.Free(addr, size);
        break;
      }
      case Action::kMaxValue:
        break;
    }
  }

  return 0;
}
