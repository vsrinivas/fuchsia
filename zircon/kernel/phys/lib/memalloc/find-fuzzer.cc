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

  std::vector<std::byte> bytes = provider.ConsumeRemainingBytes<std::byte>();
  cpp20::span<MemRange> ranges = RangesFromBytes(bytes);

  memalloc::FindNormalizedRamRanges(ranges, [](const MemRange& range) { return true; });
  return 0;
}
