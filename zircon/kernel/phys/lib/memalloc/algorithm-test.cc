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

void CompareRanges(cpp20::span<const memalloc::MemRange> expected,
                   cpp20::span<const memalloc::MemRange> actual) {
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
  const size_t scratch_size = 4 * input.size() * sizeof(void*);
  auto scratch = std::make_unique<void*[]>(scratch_size);
  std::vector<memalloc::MemRange> actual;
  memalloc::FindNormalizedRanges(input, {scratch.get(), scratch_size},
                                 [&actual](const memalloc::MemRange& range) {
                                   actual.push_back(range);
                                   return true;
                                 });
  ASSERT_NO_FATAL_FAILURE(CompareRanges(expected, {actual}));
}

TEST(MemallocFindTests, NoRanges) {
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({}, {}));
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({}, {}));
}

TEST(MemallocFindTests, OneRamRange) {
  memalloc::MemRange ranges[] = {
      //  RAM: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
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
      {.addr = 10, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // RAM: [10, 30)
      {.addr = 10, .size = 20, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, TwoAdjacentRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [10, 15)
      {.addr = 10, .size = 5, .type = memalloc::Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // [10, 30)
      {.addr = 10, .size = 20, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, TwoFullyDisjointRamRanges) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // [15, 30)
      {.addr = 15, .size = 15, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
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

  const memalloc::MemRange normalized_ram[] = {
      // [0, 5)
      {.addr = 0, .size = 5, .type = memalloc::Type::kFreeRam},
      // [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kFreeRam},
      // [60, UINT64_MAX)
      {.addr = 60, .size = kMax - 60, .type = memalloc::Type::kFreeRam},
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

  const memalloc::MemRange normalized[] = {
      // [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {normalized}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
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

  const memalloc::MemRange normalized_ram[] = {
      // [10, 15)
      {.addr = 10, .size = 5, .type = memalloc::Type::kFreeRam},
      // [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kFreeRam},
      // [40, 60)
      {.addr = 40, .size = 20, .type = memalloc::Type::kFreeRam},
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

TEST(MemallocFindTests, OverlapPrecedence) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 5), dominated by the next reserved range.
      {.addr = 0, .size = 5, .type = Type::kFreeRam},
      // peripheral: [0, 10), dominated by the next reserved range.
      {.addr = 0, .size = 10, .type = Type::kPeripheral},
      // reserved: [0, 20), dominated by no other range.
      {.addr = 0, .size = 20, .type = Type::kReserved},
      // RAM: [30, 40), dominated by the next peripheral range.
      {.addr = 30, .size = 10, .type = Type::kFreeRam},
      // peripheral: [30, 50), dominated by no other range.
      {.addr = 30, .size = 20, .type = Type::kPeripheral},
  };

  const memalloc::MemRange normalized[] = {
      // reserved: [0, 20).
      {.addr = 0, .size = 20, .type = Type::kReserved},
      // peripheral: [30, 50), dominated by no other range.
      {.addr = 30, .size = 20, .type = Type::kPeripheral},
  };

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRamRanges({ranges}, {}));

  Shuffle(ranges);
  ASSERT_NO_FATAL_FAILURE(TestFindNormalizedRanges({ranges}, {normalized}));
}

TEST(MemallocFindTests, CanShortCircuit) {
  memalloc::MemRange ranges[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // peripheral: [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kPeripheral},
      // reserved: [40, 50)
      {.addr = 40, .size = 10, .type = memalloc::Type::kReserved},
      // RAM: [60, 70)
      {.addr = 60, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [80, 90)
      {.addr = 80, .size = 10, .type = memalloc::Type::kFreeRam},
  };

  const memalloc::MemRange normalized[] = {
      // RAM: [0, 10)
      {.addr = 0, .size = 10, .type = memalloc::Type::kFreeRam},
      // peripheral: [20, 30)
      {.addr = 20, .size = 10, .type = memalloc::Type::kPeripheral},
      // reserved: [40, 50)
      {.addr = 40, .size = 10, .type = memalloc::Type::kReserved},
      // RAM: [60, 70)
      {.addr = 60, .size = 10, .type = memalloc::Type::kFreeRam},
      // RAM: [80, 90)
      {.addr = 80, .size = 10, .type = memalloc::Type::kFreeRam},
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
  memalloc::FindNormalizedRanges({ranges}, scratch, record_first_n(1));
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
  memalloc::FindNormalizedRanges({ranges}, scratch, record_first_n(2));
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
  memalloc::FindNormalizedRanges({ranges}, scratch, record_first_n(3));
  ASSERT_EQ(3u, outputs.size());
  EXPECT_EQ(normalized[0], outputs[0]);
  EXPECT_EQ(normalized[1], outputs[1]);
  EXPECT_EQ(normalized[2], outputs[2]);
}

}  // namespace
