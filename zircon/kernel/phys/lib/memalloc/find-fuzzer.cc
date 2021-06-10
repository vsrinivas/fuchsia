// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>

#include <cstddef>
#include <memory>
#include <vector>

#include <fuzzer/FuzzedDataProvider.h>

#include "algorithm.h"

namespace {

using memalloc::MemRange;

cpp20::span<MemRange> RangesFromBytes(std::vector<std::byte> bytes) {
  void* ptr = static_cast<void*>(bytes.data());
  size_t space = bytes.size();
  for (size_t size = space; size > 0; --size) {
    if (void* aligned = std::align(alignof(MemRange), size, ptr, space); aligned) {
      return {static_cast<MemRange*>(aligned), size / sizeof(MemRange)};
    }
  }
  return {};
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider provider(data, size);

  const bool just_ram = provider.ConsumeBool();

  size_t num_bytes = provider.ConsumeIntegralInRange<size_t>(0, provider.remaining_bytes());
  std::vector<std::byte> bytes = provider.ConsumeBytes<std::byte>(num_bytes);
  cpp20::span<MemRange> ranges = RangesFromBytes(bytes);

  std::vector<std::byte> aux_bytes = provider.ConsumeRemainingBytes<std::byte>();
  cpp20::span<MemRange> aux_ranges = RangesFromBytes(aux_bytes);

  constexpr auto find_all = [](const MemRange& range) { return true; };
  memalloc::MemRangeStream stream{ranges, aux_ranges};
  if (just_ram) {
    memalloc::FindNormalizedRamRanges(stream, find_all);
  } else {
    const size_t scratch_size = 4 * ranges.size() * sizeof(void*);
    auto scratch = std::make_unique<void*[]>(scratch_size);
    memalloc::FindNormalizedRanges(stream, {scratch.get(), scratch_size}, find_all);
  }
  return 0;
}
