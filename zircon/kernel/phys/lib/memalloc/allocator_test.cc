// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/memalloc.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <array>
#include <cstdlib>
#include <memory>

#include <fbl/span.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace memalloc {
namespace {

// GMock matcher to determine if a given zx::status result was successful or failed.
MATCHER(IsOk, "") { return arg.is_ok(); }
MATCHER_P(HasValue, val, "") { return arg.is_ok() && arg.value() == val; }
MATCHER_P(HasError, val, "") { return arg.is_error() && arg.error_value() == val; }

// As above, but also matches against the value:
//
// EXPECT_THAT(view.operation(), IsOkAndHolds(Eq(3u)));
template <typename X>
auto IsOkAndHolds(X matcher) {
  return ::testing::AllOf(
      IsOk(), ::testing::ResultOf([](const auto& val) { return val.value(); }, matcher));
}

// Create an Allocator and associated storage.
template <size_t Elements = 100>
struct AllocatorAndStorage {
  AllocatorAndStorage() : data(), allocator(fbl::Span(data.data(), data.size())) {}
  std::array<Range, Elements> data;
  Allocator allocator;
};

TEST(Allocator, EmptyAllocator) {
  Allocator allocator{fbl::Span<Range>{}};
  EXPECT_THAT(allocator.Allocate(1), HasError(ZX_ERR_NO_RESOURCES));
}

TEST(Allocator, ZeroSizeRanges) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Add an empty range to the allocator.
  EXPECT_THAT(allocator.AddRange(0, 0), IsOk());

  // Add a real range to the allocator.
  EXPECT_THAT(allocator.AddRange(100, 300), IsOk());

  // Allocate some empty ranges.
  EXPECT_THAT(allocator.Allocate(0), HasValue(0u));
  EXPECT_THAT(allocator.Allocate(0), HasValue(0u));

  // Allocate some real ranges again.
  EXPECT_THAT(allocator.Allocate(200), HasValue(100u));
}

TEST(Allocator, SingleRange) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Create an allocator with a single range in it.
  ASSERT_THAT(allocator.AddRange(100, 300), IsOk());

  // Expect to be able to allocate it again.
  EXPECT_THAT(allocator.Allocate(200), HasValue(100u));

  // Ensure we are empty.
  EXPECT_THAT(allocator.Allocate(200), HasError(ZX_ERR_NO_RESOURCES));
}

TEST(Allocator, MultipleAllocations) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Create an allocator with a range of size 100.
  ASSERT_THAT(allocator.AddRange(100, 100), IsOk());

  // Allocate three subranges.
  uint64_t a = allocator.Allocate(10).value_or(0);
  ASSERT_NE(a, 0u);
  uint64_t b = allocator.Allocate(20).value_or(0);
  ASSERT_NE(b, 0u);
  uint64_t c = allocator.Allocate(70).value_or(0);
  ASSERT_NE(c, 0u);

  // Ensure the allocator is empty.
  EXPECT_THAT(allocator.Allocate(1), HasError(ZX_ERR_NO_RESOURCES));

  // Try adding pages back again in a different order.
  EXPECT_THAT(allocator.AddRange(a, 10), IsOk());
  EXPECT_THAT(allocator.AddRange(c, 70), IsOk());
  EXPECT_THAT(allocator.AddRange(b, 20), IsOk());

  // We should be able to allocate the entire original range again.
  EXPECT_THAT(allocator.Allocate(100), HasValue(100u));
}

TEST(Allocator, AlignedAllocations) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Create a large range.
  ASSERT_THAT(allocator.AddRange(1, 16 * 1024 * 1024), IsOk());

  // Allocate ranges, at increasing alignment.
  for (size_t alignment = 1; alignment <= 1024 * 1024; alignment *= 2) {
    uint64_t result = allocator.Allocate(1, alignment).value_or(0);
    ASSERT_NE(result, 0u);
    EXPECT_EQ(result % alignment, 0u) << "Allocation was not aligned as requested.";
  }
}

TEST(Allocator, DeallocationMerging) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Add a range of size 4 into the allocator.
  ASSERT_THAT(allocator.AddRange(1, 4), IsOk());

  // Allocate 4 the four units and deallocate them again in every possible order.
  //
  // We attempt all 4 factorial methods of deallocating them to exercise the merging logic.
  std::vector<size_t> permutation = {0, 1, 2, 3};
  do {
    std::vector<uint64_t> values;

    // Allocate 4 values.
    for (int i = 0; i < 4; i++) {
      zx::status<uint64_t> result = allocator.Allocate(1);
      ASSERT_THAT(result, IsOk());
      values.push_back(result.value());
    }

    // Deallocate in a given order.
    for (int i = 0; i < 4; i++) {
      ASSERT_THAT(allocator.AddRange(values[permutation[i]], 1), IsOk());
    }

    // Ensure we can allocate the full range again.
    zx::status<uint64_t> result = allocator.Allocate(4);
    ASSERT_THAT(result, IsOk());
    ASSERT_EQ(result.value(), 1u);
    ASSERT_THAT(allocator.AddRange(1, 4), IsOk());
  } while (std::next_permutation(permutation.begin(), permutation.end()));
}

TEST(Allocator, Overflow) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  constexpr uint64_t kMaxAlign = uint64_t{1} << 63;

  // Add a range of size 1024 to the allocator.
  ASSERT_THAT(allocator.AddRange(1, 1024), IsOk());

  // Attempt to allocate various amounts likely to cause overflow in internal calculations.
  EXPECT_THAT(allocator.Allocate(UINT64_MAX, 1), HasError(ZX_ERR_NO_RESOURCES));
  EXPECT_THAT(allocator.Allocate(1, kMaxAlign), HasError(ZX_ERR_NO_RESOURCES));
  EXPECT_THAT(allocator.Allocate(UINT64_MAX, kMaxAlign), HasError(ZX_ERR_NO_RESOURCES));
  EXPECT_THAT(allocator.Allocate(kMaxAlign, kMaxAlign), HasError(ZX_ERR_NO_RESOURCES));
}

TEST(Allocator, OverlappingAllocations) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Create several overlapping allocations, eventually covering [0, 10).
  EXPECT_THAT(allocator.AddRange(1, 1), IsOk());  // [1, 2)
  EXPECT_THAT(allocator.AddRange(3, 1), IsOk());  // [3, 4)
  EXPECT_THAT(allocator.AddRange(5, 1), IsOk());  // [5, 6)
  EXPECT_THAT(allocator.AddRange(7, 1), IsOk());  // [7, 8)
  EXPECT_THAT(allocator.AddRange(5, 5), IsOk());  // [5, 10)
  EXPECT_THAT(allocator.AddRange(0, 5), IsOk());  // [0, 5)

  // We should be able to allocate a range of size 10, but no more.
  EXPECT_THAT(allocator.Allocate(10), IsOk());
  EXPECT_THAT(allocator.Allocate(1), HasError(ZX_ERR_NO_RESOURCES));
}

TEST(Allocator, FullRange) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Add ranges that will cause the full 2**64 space to be filled.
  //
  // Because the range has 2**64 elements, but we can only pass in
  // a range of length (2**64 - 1), we do this in two calls.
  ASSERT_THAT(allocator.AddRange(0, 1), IsOk());
  ASSERT_THAT(allocator.AddRange(1, UINT64_MAX), IsOk());

  // Ensure we can allocate the full range.
  zx::status<uint64_t> a = allocator.Allocate(0x80000000'00000000);
  ASSERT_THAT(a, IsOk());
  zx::status<uint64_t> b = allocator.Allocate(0x80000000'00000000);
  ASSERT_THAT(b, IsOk());
}

// Add the given list of ranges, and then remove the second list of ranges. Return the
// number of elements in the remaining range.
uint64_t AddThenRemove(const std::vector<std::pair<uint64_t, uint64_t>>& add,
                       const std::vector<std::pair<uint64_t, uint64_t>>& remove) {
  AllocatorAndStorage storage;
  Allocator& allocator = storage.allocator;

  // Add the first list of ranges, then remove the second.
  for (const auto& range : add) {
    (void)allocator.AddRange(range.first, range.second);
  }
  for (const auto& range : remove) {
    (void)allocator.RemoveRange(range.first, range.second);
  }

  // Keep allocating items from the allocator, until we can't allocate any more.
  uint64_t items_left = 0;
  while (allocator.Allocate(1).is_ok()) {
    items_left++;
  }
  return items_left;
}

TEST(Allocator, RemoveRange) {
  // Remove range that doesn't exist.
  EXPECT_EQ(AddThenRemove({}, {{0, 10}}), 0u);

  // Remove full range.
  EXPECT_EQ(AddThenRemove({{0, 10}}, {{0, 10}}), 0u);

  // Remove area larger than a range.
  EXPECT_EQ(AddThenRemove({{1, 8}}, {{0, 10}}), 0u);

  // Remove area covering two ranges.
  EXPECT_EQ(AddThenRemove({{1, 1}, {8, 1}}, {{0, 10}}), 0u);

  // Remove end of a range.
  EXPECT_EQ(AddThenRemove({{0, 10}}, {{5, 10}}), 5u);

  // Remove beginning of a range.
  EXPECT_EQ(AddThenRemove({{5, 10}}, {{0, 10}}), 5u);

  // Remove middle of a range.
  EXPECT_EQ(AddThenRemove({{0, 10}}, {{5, 2}}), 8u);

  // Remove end of one range and the beginning of another.
  EXPECT_EQ(AddThenRemove({{0, 2}, {8, 2}}, {{1, 8}}), 2u);
}

}  // namespace
}  // namespace memalloc
