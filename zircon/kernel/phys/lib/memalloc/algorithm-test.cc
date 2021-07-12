// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "algorithm.h"

#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "test.h"

namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

using memalloc::MemRangeStream;
using memalloc::Type;
using memalloc::internal::MemRangeIterationContext;

void TestFindNormalizedRamRanges(cpp20::span<memalloc::MemRange> input,
                                 cpp20::span<const memalloc::MemRange> expected) {
  std::vector<memalloc::MemRange> actual;
  memalloc::FindNormalizedRamRanges(input, [&actual](const memalloc::MemRange& range) {
    actual.push_back(range);
    return true;
  });
  ASSERT_NO_FATAL_FAILURE(CompareRanges(expected, {actual}));
}

void TestFindNormalizedRanges(cpp20::span<memalloc::MemRange> input,
                              cpp20::span<const memalloc::MemRange> expected) {
  const size_t scratch_size = memalloc::FindNormalizedRangesScratchSize(input.size());
  auto scratch = std::make_unique<void*[]>(scratch_size);
  std::vector<memalloc::MemRange> actual;
  auto result = memalloc::FindNormalizedRanges(input, {scratch.get(), scratch_size},
                                               [&actual](const memalloc::MemRange& range) {
                                                 actual.push_back(range);
                                                 return true;
                                               });
  ASSERT_FALSE(result.is_error());
  ASSERT_NO_FATAL_FAILURE(CompareRanges(expected, {actual}));
}

void ExpectBadOverlap(cpp20::span<memalloc::MemRange> input) {
  const size_t scratch_size = memalloc::FindNormalizedRangesScratchSize(input.size());
  auto scratch = std::make_unique<void*[]>(scratch_size);
  constexpr auto noop = [](const memalloc::MemRange& range) { return true; };
  auto result = memalloc::FindNormalizedRanges(input, {scratch.get(), scratch_size}, noop);
  ASSERT_TRUE(result.is_error());
}

void TestMemRangeStream(cpp20::span<cpp20::span<memalloc::MemRange>> inputs,
                        cpp20::span<const memalloc::MemRange> expected) {
  std::vector<MemRangeIterationContext> state(inputs.begin(), inputs.end());
  memalloc::MemRangeStream stream({state});

  size_t num_ranges = 0;
  for (auto input : inputs) {
    num_ranges += input.size();
  }
  EXPECT_EQ(num_ranges, stream.size());

  std::vector<memalloc::MemRange> actual;
  for (const memalloc::MemRange* range = stream(); range; range = stream()) {
    actual.push_back(*range);
  }
  EXPECT_EQ(actual.size(), stream.size());
  EXPECT_EQ(actual.empty(), stream.empty());
  ASSERT_NO_FATAL_FAILURE(CompareRanges(expected, {actual}));

  // Repeated calls should yield nullptr.
  EXPECT_EQ(nullptr, stream());
  EXPECT_EQ(nullptr, stream());
  EXPECT_EQ(nullptr, stream());

  // Resetting the stream should put it back in its initial state.
  stream.reset();
  actual.clear();
  for (const memalloc::MemRange* range = stream(); range; range = stream()) {
    actual.push_back(*range);
  }
  EXPECT_EQ(actual.size(), stream.size());
  EXPECT_EQ(actual.empty(), stream.empty());
  ASSERT_NO_FATAL_FAILURE(CompareRanges(expected, {actual}));
}

TEST(MemallocFindTests, NoRanges) {
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({}, {}));
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({}, {}));
}

TEST(MemallocFindTests, OneRamRange) {
  memalloc::MemRange ranges[] = {
      //  RAM: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
  };
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {ranges}));
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {ranges}));
}

TEST(MemallocFindTests, OneNonRamRange) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
  };
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {}));
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {ranges}));
}

TEST(MemallocFindTests, MultipleNonRamRanges) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
      // reserved: [5, 15)
      {.addr = 5, .size = 10, .type = Type::kReserved},
      // reserved: [15, 20)
      {.addr = 15, .size = 5, .type = Type::kReserved},
      // peripheral: [25, 30)
      {.addr = 25, .size = 5, .type = Type::kPeripheral},
  };

  const memalloc::MemRange normalized[] = {
      // reserved: [0, 20)
      {.addr = 0, .size = 20, .type = Type::kReserved},
      // peripheral: [25, 30)
      {.addr = 25, .size = 5, .type = Type::kPeripheral},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, TwoIntersectingRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [10, 20)
      {.addr = 10, .size = 10, .type = Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // RAM: [10, 30)
      {.addr = 10, .size = 20, .type = Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, TwoAdjacentRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [10, 15)
      {.addr = 10, .size = 5, .type = Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // [10, 30)
      {.addr = 10, .size = 20, .type = Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, TwoFullyDisjointRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // [15, 30)
      {.addr = 15, .size = 15, .type = Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, MixedFullyDisjointRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 5)
      {.addr = 0, .size = 5, .type = Type::kFreeRam},
      // reserved: [10, 15)
      {.addr = 10, .size = 5, .type = Type::kReserved},
      // RAM: [20, 30)
      {.addr = 20, .size = 10, .type = Type::kFreeRam},
      // peripheral: [40, 45)
      {.addr = 40, .size = 5, .type = Type::kPeripheral},
      // reserved: [50, 55)
      {.addr = 50, .size = 5, .type = Type::kReserved},
      // RAM: [60, UINT64_MAX)
      {.addr = 60, .size = kMax - 60, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized_ram[] = {
      // [0, 5)
      {.addr = 0, .size = 5, .type = Type::kFreeRam},
      // [20, 30)
      {.addr = 20, .size = 10, .type = Type::kFreeRam},
      // [60, UINT64_MAX)
      {.addr = 60, .size = kMax - 60, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // RAM: [0, 5)
      {.addr = 0, .size = 5, .type = Type::kFreeRam},
      // reserved: [10, 15)
      {.addr = 10, .size = 5, .type = Type::kReserved},
      // RAM: [20, 30)
      {.addr = 20, .size = 10, .type = Type::kFreeRam},
      // peripheral: [40, 45)
      {.addr = 40, .size = 5, .type = Type::kPeripheral},
      // reserved: [50, 55)
      {.addr = 50, .size = 5, .type = Type::kReserved},
      // RAM: [60, UINT64_MAX)
      {.addr = 60, .size = kMax - 60, .type = Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized_ram}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, HighlyIntersectingLikeRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 5)
      {.addr = 0, .size = 5, .type = Type::kFreeRam},
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // RAM: [1, 6)
      {.addr = 1, .size = 5, .type = Type::kFreeRam},
      // RAM: [1, 10)
      {.addr = 1, .size = 9, .type = Type::kFreeRam},
      // RAM: [2, 7)
      {.addr = 2, .size = 5, .type = Type::kFreeRam},
      // RAM: [2, 10)
      {.addr = 2, .size = 8, .type = Type::kFreeRam},
      // RAM: [3, 8)
      {.addr = 3, .size = 5, .type = Type::kFreeRam},
      // RAM: [3, 10)
      {.addr = 3, .size = 7, .type = Type::kFreeRam},
      // RAM: [4, 9)
      {.addr = 4, .size = 5, .type = Type::kFreeRam},
      // RAM: [4, 10)
      {.addr = 4, .size = 6, .type = Type::kFreeRam},
      // RAM: [5, 5)
      {.addr = 5, .size = 5, .type = Type::kFreeRam},
      // RAM: [5, 5)
      {.addr = 5, .size = 5, .type = Type::kFreeRam},
      // RAM: [6, 10)
      {.addr = 6, .size = 4, .type = Type::kFreeRam},
      // RAM: [7, 10)
      {.addr = 7, .size = 3, .type = Type::kFreeRam},
      // RAM: [8, 10)
      {.addr = 8, .size = 2, .type = Type::kFreeRam},
      // RAM: [9, 10)
      {.addr = 9, .size = 1, .type = Type::kFreeRam},
      // RAM: [10, 10) (i.e., Ø).
      {.addr = 10, .size = 0, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, MixedRanges1) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
      // RAM: [5, 15), though we only expect [10, 15) to be free.
      {.addr = 5, .size = 10, .type = Type::kFreeRam},
      // RAM: [20, 60), though we only expect to [20, 30) and [40, 60) to be free.
      {.addr = 20, .size = 40, .type = Type::kFreeRam},
      // reserved: [30, 35)
      {.addr = 30, .size = 5, .type = Type::kReserved},
      // reserved: [35, 40)
      {.addr = 35, .size = 5, .type = Type::kReserved},
      // peripheral: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kPeripheral},
  };

  const memalloc::MemRange normalized_ram[] = {
      // [10, 15)
      {.addr = 10, .size = 5, .type = Type::kFreeRam},
      // [20, 30)
      {.addr = 20, .size = 10, .type = Type::kFreeRam},
      // [40, 60)
      {.addr = 40, .size = 20, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
      // RAM: [10, 15)
      {.addr = 10, .size = 5, .type = Type::kFreeRam},
      // RAM: [20, 30)
      {.addr = 20, .size = 10, .type = Type::kFreeRam},
      // reserved: [30, 40)
      {.addr = 30, .size = 10, .type = Type::kReserved},
      // RAM: [40, 60)
      {.addr = 40, .size = 20, .type = Type::kFreeRam},
      // peripheral: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kPeripheral},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized_ram}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, MixedRanges2) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 60)
      {.addr = 0, .size = 60, .type = Type::kReserved},
      // RAM: [5, 90)
      {.addr = 5, .size = 85, .type = Type::kFreeRam},
      // RAM: [10, 40)
      {.addr = 10, .size = 30, .type = Type::kFreeRam},
      // reserved: [80, 100)
      {.addr = 80, .size = 20, .type = Type::kReserved},
  };

  const memalloc::MemRange normalized_ram[] = {
      // RAM: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // reserved: [0, 60)
      {.addr = 0, .size = 60, .type = Type::kReserved},
      // RAM: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kFreeRam},
      // reserved: [80, 100)
      {.addr = 80, .size = 20, .type = Type::kReserved},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized_ram}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, MixedRanges3) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 90)
      {.addr = 0, .size = 90, .type = Type::kFreeRam},
      // reserved: [10, 70)
      {.addr = 10, .size = 60, .type = Type::kReserved},
      // RAM: [20, 30)
      {.addr = 20, .size = 10, .type = Type::kFreeRam},
      // RAM: [40, 50)
      {.addr = 40, .size = 10, .type = Type::kFreeRam},
      // RAM: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized_ram[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // RAM: [70, 90)
      {.addr = 70, .size = 20, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // reserved: [10, 70)
      {.addr = 10, .size = 60, .type = Type::kReserved},
      // RAM: [70, 90)
      {.addr = 70, .size = 20, .type = Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized_ram}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, OverlapPrecedence) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 10), dominated by the next reserved range.
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // peripheral: [0, 20), dominated by the next reserved range.
      {.addr = 0, .size = 20, .type = Type::kPeripheral},
      // reserved: [0, 30), dominated by no other range.
      {.addr = 0, .size = 30, .type = Type::kReserved},

      // RAM: [30, 40), dominated by the next peripheral range.
      {.addr = 30, .size = 10, .type = Type::kFreeRam},
      // peripheral: [40, 50), dominated by no other range.
      {.addr = 30, .size = 20, .type = Type::kPeripheral},

      // RAM: [50, 60), dominated by the next range of extended type.
      {.addr = 50, .size = 10, .type = Type::kFreeRam},
      // phys kernel image: [50, 70), dominated by no other range.
      {.addr = 50, .size = 20, .type = Type::kPhysKernel},

      // RAM: [70, 80), dominated by no other range.
      {.addr = 70, .size = 10, .type = Type::kFreeRam},

      // phys kernel image: [80, 90), merged into nearby like ranges.
      {.addr = 80, .size = 10, .type = Type::kPhysKernel},
      // phys kernel image: [80, 100).
      {.addr = 80, .size = 20, .type = Type::kPhysKernel},
  };

  const memalloc::MemRange normalized_ram[] = {
      // [70, 80).
      {.addr = 70, .size = 10, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // reserved: [0, 30).
      {.addr = 0, .size = 30, .type = Type::kReserved},
      // peripheral: [30, 50)
      {.addr = 30, .size = 20, .type = Type::kPeripheral},
      // phys kernel image: [50, 70), dominated by no other range.
      {.addr = 50, .size = 20, .type = Type::kPhysKernel},
      // RAM: [70, 80).
      {.addr = 70, .size = 10, .type = Type::kFreeRam},
      // phys kernel image: [80, 100).
      {.addr = 80, .size = 20, .type = Type::kPhysKernel},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized_ram}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, BadOverlaps) {
  // Extended with extended.
  {
    memalloc::MemRange ranges[] = {
        // phys kernel image: [0, 10)
        {.addr = 0, .size = 10, .type = Type::kPhysKernel},
        // data ZBI : [5, 10)
        {.addr = 5, .size = 5, .type = Type::kDataZbi},
    };
    ASSERT_NO_FATAL_FAILURE(ExpectBadOverlap({ranges}));
  }

  // Extended with reserved.
  {
    memalloc::MemRange ranges[] = {
        // phys kernel image: [0, 10)
        {.addr = 0, .size = 10, .type = Type::kPhysKernel},
        // reserved: [0, 20)
        {.addr = 0, .size = 20, .type = Type::kReserved},
    };
    ASSERT_NO_FATAL_FAILURE(ExpectBadOverlap({ranges}));
  }

  // Extended with reserved.
  {
    memalloc::MemRange ranges[] = {
        // phys kernel image: [0, 10)
        {.addr = 0, .size = 10, .type = Type::kPhysKernel},
        // reserved: [0, 20)
        {.addr = 0, .size = 20, .type = Type::kPeripheral},
    };
    ASSERT_NO_FATAL_FAILURE(ExpectBadOverlap({ranges}));
  }
}

TEST(MemallocFindTests, CanShortCircuit) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // peripheral: [20, 30)
      {.addr = 20, .size = 10, .type = Type::kPeripheral},
      // reserved: [40, 50)
      {.addr = 40, .size = 10, .type = Type::kReserved},
      // RAM: [60, 70)
      {.addr = 60, .size = 10, .type = Type::kFreeRam},
      // RAM: [80, 90)
      {.addr = 80, .size = 10, .type = Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kFreeRam},
      // peripheral: [20, 30)
      {.addr = 20, .size = 10, .type = Type::kPeripheral},
      // reserved: [40, 50)
      {.addr = 40, .size = 10, .type = Type::kReserved},
      // RAM: [60, 70)
      {.addr = 60, .size = 10, .type = Type::kFreeRam},
      // RAM: [80, 90)
      {.addr = 80, .size = 10, .type = Type::kFreeRam},
  };

  const size_t scratch_size = 4 * sizeof(ranges) * sizeof(void*);
  auto scratch_ptr = std::make_unique<void*[]>(scratch_size);
  cpp20::span<void*> scratch{scratch_ptr.get(), scratch_size};

  std::vector<memalloc::MemRange> outputs;

  auto record_first_n = [&outputs](size_t n) -> memalloc::MemRangeCallback {
    ZX_ASSERT(n > 0);
    return [&outputs, countdown = n](const memalloc::MemRange& range) mutable {
      outputs.push_back(range);
      return --countdown > 0;
    };
  };

  // Only record the first RAM range.
  Shuffle(ranges);
  memalloc::FindNormalizedRamRanges({ranges}, record_first_n(1));
  ASSERT_EQ(1u, outputs.size());
  EXPECT_EQ(normalized[0], outputs[0]);

  // Only record the first range.
  Shuffle(ranges);
  outputs.clear();
  auto result = memalloc::FindNormalizedRanges({ranges}, scratch, record_first_n(1));
  ASSERT_FALSE(result.is_error());
  ASSERT_EQ(1u, outputs.size());
  EXPECT_EQ(normalized[0], outputs[0]);

  // Only record the first two RAM ranges.
  outputs.clear();
  Shuffle(ranges);
  memalloc::FindNormalizedRamRanges({ranges}, record_first_n(2));
  ASSERT_EQ(2u, outputs.size());
  EXPECT_EQ(normalized[0], outputs[0]);
  EXPECT_EQ(normalized[3], outputs[1]);

  // Only record the first two ranges.
  outputs.clear();
  Shuffle(ranges);
  result = memalloc::FindNormalizedRanges({ranges}, scratch, record_first_n(2));
  ASSERT_FALSE(result.is_error());
  ASSERT_EQ(2u, outputs.size());
  EXPECT_EQ(normalized[0], outputs[0]);
  EXPECT_EQ(normalized[1], outputs[1]);

  // Only record the first three RAM ranges.
  outputs.clear();
  Shuffle(ranges);
  memalloc::FindNormalizedRamRanges({ranges}, record_first_n(3));
  ASSERT_EQ(3u, outputs.size());
  EXPECT_EQ(normalized[0], outputs[0]);
  EXPECT_EQ(normalized[3], outputs[1]);
  EXPECT_EQ(normalized[4], outputs[2]);

  // Only record the first three ranges.
  outputs.clear();
  Shuffle(ranges);
  result = memalloc::FindNormalizedRanges({ranges}, scratch, record_first_n(3));
  ASSERT_FALSE(result.is_error());
  ASSERT_EQ(3u, outputs.size());
  EXPECT_EQ(normalized[0], outputs[0]);
  EXPECT_EQ(normalized[1], outputs[1]);
  EXPECT_EQ(normalized[2], outputs[2]);
}

TEST(MemallocRangeStreamTests, Empty) {
  memalloc::MemRangeStream stream({});
  EXPECT_TRUE(stream.empty());
  EXPECT_EQ(0u, stream.size());

  ASSERT_NO_FATAL_FAILURE(TestMemRangeStream({}, {}));
}

TEST(MemallocRangeStreamTests, OutputIsSorted) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
      // RAM: [5, 15)
      {.addr = 5, .size = 10, .type = Type::kFreeRam},
      // RAM: [20, 60)
      {.addr = 20, .size = 40, .type = Type::kFreeRam},
      // reserved: [30, 35)
      {.addr = 30, .size = 5, .type = Type::kReserved},
      // reserved: [35, 40)
      {.addr = 35, .size = 5, .type = Type::kReserved},
      // peripheral: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kPeripheral},
  };

  std::default_random_engine engine{0xc0ffee};
  std::uniform_int_distribution<size_t> dist(0, std::size(ranges));
  auto make_partition = [&](std::vector<cpp20::span<memalloc::MemRange>>& parts) {
    size_t idx = 0;
    while (idx < std::size(ranges)) {
      size_t part_size = (dist(engine) % (std::size(ranges) - idx)) + 1;
      parts.push_back({std::begin(ranges) + idx, part_size});
      idx += part_size;
    }
  };

  const memalloc::MemRange expected[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
      // RAM: [5, 15), though we only expect [10, 15) to be free.
      {.addr = 5, .size = 10, .type = Type::kFreeRam},
      // RAM: [20, 60), though we only expect to [20, 30) and [40, 60) to be free.
      {.addr = 20, .size = 40, .type = Type::kFreeRam},
      // reserved: [30, 35)
      {.addr = 30, .size = 5, .type = Type::kReserved},
      // reserved: [35, 40)
      {.addr = 35, .size = 5, .type = Type::kReserved},
      // peripheral: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kPeripheral},
  };

  for (size_t i = 0; i < 100; ++i) {
    Shuffle(ranges);
    std::vector<cpp20::span<memalloc::MemRange>> parts;
    make_partition(parts);
    ASSERT_NO_FATAL_FAILURE(TestMemRangeStream({parts}, expected));
  }
}

}  // namespace
