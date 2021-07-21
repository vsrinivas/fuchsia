// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_TEST_H_
#define ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_TEST_H_

#include <lib/memalloc/pool.h>
#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <stdio.h>

#include <algorithm>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "algorithm.h"
#include "ostream.h"  // Enables MemRange auto-stringification in gtest error messages

//
// Shared test utilities.
//

// PoolContext holds both a Pool and the memory that backs its bookkeeping.
// To decouple memory ranges that are tracked from memory ranges that are
// actually used, we translate any nominal bookkeeping space to the heap.
struct PoolContext {
  std::vector<std::unique_ptr<std::byte[]>> bookkeeping;

  memalloc::Pool pool = memalloc::Pool([this](uint64_t addr, uint64_t size) {
    bookkeeping.push_back(std::make_unique<std::byte[]>(size));
    return bookkeeping.back().get();
  });
};

inline std::string ToString(const memalloc::MemRange& range) {
  std::stringstream ss;
  ss << range;
  return ss.str();
}

template <typename MemRangeIter>
inline std::string ToString(MemRangeIter first, MemRangeIter last) {
  std::string s;
  for (auto it = first; it != last; ++it) {
    s += "  " + ToString(*it) + "\n";
  }
  return s;
}

inline std::string ToString(const std::vector<const memalloc::MemRange>& ranges) {
  return ToString(ranges.begin(), ranges.end());
}

inline std::string ToString(const cpp20::span<const memalloc::MemRange>& ranges) {
  return ToString(ranges.begin(), ranges.end());
}

template <size_t N>
inline void Shuffle(memalloc::MemRange (&ranges)[N]) {
  static std::default_random_engine engine{0xc0ffee};
  std::shuffle(std::begin(ranges), std::end(ranges), engine);
}

inline void CompareRanges(cpp20::span<const memalloc::MemRange> expected,
                          cpp20::span<const memalloc::MemRange> actual) {
  EXPECT_EQ(expected.size(), actual.size());
  size_t num_comparable = std::min(expected.size(), actual.size());
  for (size_t i = 0; i < num_comparable; ++i) {
    EXPECT_EQ(expected[i], actual[i]);
  }

  if (expected.size() > num_comparable) {
    printf("Unaccounted for expected ranges:\n%s",
           ToString(expected.begin() + num_comparable, expected.end()).c_str());
  }
  if (actual.size() > num_comparable) {
    printf("Unaccounted for actual ranges:\n%s",
           ToString(actual.begin() + num_comparable, actual.end()).c_str());
  }
}

inline cpp20::span<memalloc::MemRange> RangesFromBytes(const std::vector<std::byte>& bytes) {
  void* ptr = const_cast<void*>(static_cast<const void*>(bytes.data()));
  size_t space = bytes.size();
  for (size_t size = space; size > 0; --size) {
    if (void* aligned = std::align(alignof(memalloc::MemRange), size, ptr, space); aligned) {
      return {static_cast<memalloc::MemRange*>(aligned), size / sizeof(memalloc::MemRange)};
    }
  }
  return {};
}

#endif  // ZIRCON_KERNEL_PHYS_LIB_MEMALLOC_TEST_H_
