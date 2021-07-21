// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <zircon/assert.h>

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "test.h"

namespace {

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

  std::vector<std::byte> bytes = provider.ConsumeRemainingBytes<std::byte>();
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
  return 0;
}
