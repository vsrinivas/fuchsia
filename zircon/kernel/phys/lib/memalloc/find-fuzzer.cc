// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "algorithm.h"
#include "test.h"

namespace {

using memalloc::MemRange;

// What our fuzzer should do.
enum class Action {
  kFindRam,
  kFindAll,
  kFindBothAndCompare,
  kMaxValue,  // Required by FuzzedDataProvider::ConsumeEnum().
};

// Assumes that `ranges` is sorted.
bool Contains(const std::vector<MemRange>& ranges, const MemRange& range) {
  return std::binary_search(ranges.begin(), ranges.end(), range);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);
  std::vector<MemRange> ram, all;

  const Action action = provider.ConsumeEnum<Action>();
  std::vector<std::byte> bytes = provider.ConsumeRemainingBytes<std::byte>();
  cpp20::span<MemRange> ranges = RangesFromBytes(bytes);

  // Whether we are to exercise FindNormalizedRamRanges().
  if (action == Action::kFindRam || action == Action::kFindBothAndCompare) {
    auto find_ram = [&ram](const MemRange& range) {
      ram.push_back(range);
      return true;
    };
    memalloc::FindNormalizedRamRanges(ranges, find_ram);
    ZX_ASSERT_MSG(std::is_sorted(ram.begin(), ram.end()),
                  "output RAM ranges are not sorted:\n%s\noriginal ranges:\n%s",
                  ToString(ram).c_str(), ToString(ranges).c_str());
  }

  // Whether we are to exercise FindNormalizedRanges().
  if (action == Action::kFindAll || action == Action::kFindBothAndCompare) {
    auto find_all = [&all](const MemRange& range) {
      all.push_back(range);
      return true;
    };
    const size_t scratch_size = memalloc::FindNormalizedRangesScratchSize(ranges.size());
    auto scratch = std::make_unique<void*[]>(scratch_size);
    if (auto result =
            memalloc::FindNormalizedRanges(ranges, {scratch.get(), scratch_size}, find_all);
        result.is_error()) {
      return 0;
    }
    ZX_ASSERT_MSG(std::is_sorted(all.begin(), all.end()),
                  "output ranges are not sorted:\n%s\noriginal ranges:\n%s", ToString(all).c_str(),
                  ToString(ranges).c_str());
  }

  // Whether we have exercised both FindNormalizedRamRanges() and
  // FindNormalizedRanges(), and now wish to compare the results.
  if (action == Action::kFindBothAndCompare) {
    for (const MemRange& range : ram) {
      ZX_ASSERT_MSG(Contains(all, range),
                    "normalized RAM range (%s) not found among all normalized "
                    "ranges:\n%s\noriginal ranges:\n%s",
                    ToString(range).c_str(), ToString(all).c_str(), ToString(ranges).c_str());
    }
  }

  return 0;
}
