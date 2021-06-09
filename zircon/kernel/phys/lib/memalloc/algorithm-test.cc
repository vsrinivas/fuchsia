// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "algorithm.h"

#include <lib/memalloc/range.h>
#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ostream.h"  // Enables MemRange auto-stringification in gtest error messages

namespace {

constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();

using memalloc::Type;

std::string ToString(const memalloc::MemRange& range) {
  std::stringstream ss;
  ss << range;
  return ss.str();
}

template <size_t N>
void Shuffle(memalloc::MemRange (&ranges)[N]) {
  static std::default_random_engine engine{0xc0ffee};
  std::shuffle(std::begin(ranges), std::end(ranges), engine);
}

void TestFindNormalizedRamRanges(cpp20::span<memalloc::MemRange> input,
                                 cpp20::span<const memalloc::MemRange> expected) {
  std::vector<memalloc::MemRange> actual;
  memalloc::FindNormalizedRamRanges(input, [&actual](const memalloc::MemRange& range) {
    actual.push_back(range);
    return true;
  });

  EXPECT_EQ(expected.size(), actual.size());
  size_t num_comparable = std::min(expected.size(), actual.size());
  for (size_t i = 0; i < num_comparable; ++i) {
    EXPECT_EQ(expected[i], actual[i]);
  }

  if (expected.size() > num_comparable) {
    printf("Unaccounted for expected ranges:\n");
    for (size_t i = num_comparable; i < expected.size(); ++i) {
      printf("  %s\n", ToString(expected[i]).c_str());
    }
  }
  if (actual.size() > num_comparable) {
    printf("Unaccounted for actual ranges:\n");
    for (size_t i = num_comparable; i < actual.size(); ++i) {
      printf("  %s\n", ToString(actual[i]).c_str());
    }
  }
}

TEST(MemallocFindTests, NoRanges) { ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({}, {})); }

TEST(MemallocFindTests, OneRamRange) {
  memalloc::MemRange ranges[] = {
      //  RAM: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
  };
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {ranges}));
}

TEST(MemallocFindTests, OneNonRamRange) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
  };
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {}));
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
  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {}));
}

TEST(MemallocFindTests, TwoIntersectingRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [10, 20)
      {.addr = 10, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange expected[] = {
      // RAM: [10, 30)
      {.addr = 10, .size = 20, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {expected}));
}

TEST(MemallocFindTests, TwoAdjacentRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [10, 15)
      {.addr = 10, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange expected[] = {
      // [10, 30)
      {.addr = 10, .size = 20, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {expected}));
}

TEST(MemallocFindTests, TwoFullyDisjointRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange expected[] = {
      // [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {expected}));
}

TEST(MemallocFindTests, MixedFullyDisjointRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 5)
      {.addr = 0, .size = 5, .type = memalloc::Type::kFreeRam},
      // reserved: [10, 15)
      {.addr = 10, .size = 5, .type = Type::kReserved},
      // RAM: [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kFreeRam},
      // peripheral: [40, 45)
      {.addr = 40, .size = 5, .type = Type::kPeripheral},
      // reserved: [50, 55)
      {.addr = 50, .size = 5, .type = Type::kReserved},
      // RAM: [60, UINT64_MAX)
      {.addr = 60, .size = kMax - 60, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange expected[] = {
      // [0, 5)
      {.addr = 0, .size = 5, .type = memalloc::Type::kFreeRam},
      // [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kFreeRam},
      // [60, UINT64_MAX)
      {.addr = 60, .size = kMax - 60, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {expected}));
}

TEST(MemallocFindTests, HighlyIntersectingLikeRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 5)
      {.addr = 0, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [1, 6)
      {.addr = 1, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [1, 10)
      {.addr = 1, .size = 9, .type = memalloc::Type::kFreeRam},
      // RAM: [2, 7)
      {.addr = 2, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [2, 10)
      {.addr = 2, .size = 8, .type = memalloc::Type::kFreeRam},
      // RAM: [3, 8)
      {.addr = 3, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [3, 10)
      {.addr = 3, .size = 7, .type = memalloc::Type::kFreeRam},
      // RAM: [4, 9)
      {.addr = 4, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [4, 10)
      {.addr = 4, .size = 6, .type = memalloc::Type::kFreeRam},
      // RAM: [5, 5)
      {.addr = 5, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [5, 5)
      {.addr = 5, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [6, 10)
      {.addr = 6, .size = 4, .type = memalloc::Type::kFreeRam},
      // RAM: [7, 10)
      {.addr = 7, .size = 3, .type = memalloc::Type::kFreeRam},
      // RAM: [8, 10)
      {.addr = 8, .size = 2, .type = memalloc::Type::kFreeRam},
      // RAM: [9, 10)
      {.addr = 9, .size = 1, .type = memalloc::Type::kFreeRam},
      // RAM: [10, 10) (i.e., Ø).
      {.addr = 10, .size = 0, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange expected[] = {
      // [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {expected}));
}

TEST(MemallocFindTests, MixedRanges) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = Type::kReserved},
      // RAM: [5, 15), though we only expect [10, 15) to be free.
      {.addr = 5, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [20, 60), though we only expect to [20, 30) and [40, 60) to be free.
      {.addr = 20, .size = 40, .type = memalloc::Type::kFreeRam},
      // reserved: [30, 35)
      {.addr = 30, .size = 5, .type = Type::kReserved},
      // reserved: [35, 40)
      {.addr = 35, .size = 5, .type = Type::kReserved},
      // peripheral: [60, 80)
      {.addr = 60, .size = 20, .type = Type::kPeripheral},
  };

  const memalloc::MemRange expected[] = {
      // [10, 15)
      {.addr = 10, .size = 5, .type = memalloc::Type::kFreeRam},
      // [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kFreeRam},
      // [40, 60)
      {.addr = 40, .size = 20, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {expected}));
}

TEST(MemallocFindTests, CanShortCircuit) {
  memalloc::MemRange ranges[] = {
      // reserved: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // reserved: [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kFreeRam},
      // reserved: [40, 50)
      {.addr = 40, .size = 10, .type = memalloc::Type::kFreeRam},
      // reserved: [60, 70)
      {.addr = 60, .size = 10, .type = memalloc::Type::kFreeRam},
  };

  std::vector<memalloc::MemRange> outputs;

  // Only record the first range.
  Shuffle(ranges);
  memalloc::FindNormalizedRamRanges({ranges}, [&outputs](const memalloc::MemRange& range) mutable {
    outputs.push_back(range);
    return false;
  });
  ASSERT_EQ(1u, outputs.size());
  EXPECT_EQ(ranges[0], outputs[0]);

  // Only record the first two ranges.
  outputs.clear();
  Shuffle(ranges);
  memalloc::FindNormalizedRamRanges(
      {ranges}, [&outputs, count = size_t{0}](const memalloc::MemRange& range) mutable {
        outputs.push_back(range);
        return ++count < 2;
      });
  ASSERT_EQ(2u, outputs.size());
  EXPECT_EQ(ranges[0], outputs[0]);
  EXPECT_EQ(ranges[1], outputs[1]);

  // Only record the first three ranges.
  outputs.clear();
  Shuffle(ranges);
  memalloc::FindNormalizedRamRanges(
      {ranges}, [&outputs, count = size_t{0}](const memalloc::MemRange& range) mutable {
        outputs.push_back(range);
        return ++count < 3;
      });
  ASSERT_EQ(3u, outputs.size());
  EXPECT_EQ(ranges[0], outputs[0]);
  EXPECT_EQ(ranges[1], outputs[1]);
  EXPECT_EQ(ranges[2], outputs[2]);
}

}  // namespace
