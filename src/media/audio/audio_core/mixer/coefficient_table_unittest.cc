// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/coefficient_table.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/format/frames.h"

namespace media::audio::mixer {
namespace {

TEST(CoefficientTableTest, AllIndicesAccessible) {
  Fixed width(10);
  CoefficientTable table(width.raw_value(), Fixed::Format::FractionalBits);
  for (uint32_t i = 0; i < width.raw_value(); ++i) {
    table[i] = static_cast<float>(i);
  }
  for (uint32_t i = 0; i < width.raw_value(); ++i) {
    ASSERT_FLOAT_EQ(table[i], static_cast<float>(i));
  }
}

TEST(CoefficientTableTest, IntegralStrideHasPhysicallyContiguousIndicies) {
  Fixed width(10);
  CoefficientTable table(width.raw_value(), Fixed::Format::FractionalBits);

  const auto FRAC_BITS = Fixed::Format::FractionalBits;
  const auto FRAC_ONE = 1 << FRAC_BITS;
  for (uint32_t fraction = 0; fraction < FRAC_ONE; ++fraction) {
    // Each fractional value will have a block in the vector. Now check that every valid integral
    // value is contiguous for this fractional value.
    uint32_t block_index = fraction * width.Ceiling();
    for (uint32_t integer = 0; integer < width.Ceiling(); ++integer) {
      auto fixed_value = (integer << FRAC_BITS) + fraction;
      ASSERT_EQ(block_index + integer, table.PhysicalIndex(fixed_value));
    }
  }
}

TEST(CoefficientTableTest, ReadSlice) {
  Fixed width(10);
  CoefficientTable table(width.raw_value(), Fixed::Format::FractionalBits);

  const auto FRAC_BITS = Fixed::Format::FractionalBits;
  const auto FRAC_ONE = 1 << FRAC_BITS;
  for (uint32_t fraction = 0; fraction < FRAC_ONE; ++fraction) {
    auto slice = table.ReadSlice(fraction, width.Ceiling());
    ASSERT_NE(slice, nullptr);

    for (int64_t i = 0; i < width.Ceiling(); ++i) {
      ASSERT_EQ(slice[i], table[fraction + (i << Fixed::Format::FractionalBits)]);
    }
  }
}

TEST(CoefficientTableCacheTest, CachingWorks) {
  using InputT = std::pair<int, int>;

  // This function creates a new table. It's a var so it can be stubbed out before each call to Get.
  fit::function<CoefficientTable*()> create_table;

  CoefficientTableCache<InputT> cache([&create_table](InputT) { return create_table(); });
  auto cache_get = [&cache, &create_table](InputT input, fit::function<CoefficientTable*()> fn) {
    create_table = std::move(fn);
    return cache.Get(input);
  };

  CoefficientTable* t1 = new CoefficientTable(1, 1);
  CoefficientTable* t2 = new CoefficientTable(1, 1);

  auto p1 = cache_get(InputT(1, 1), [t1]() { return t1; });
  EXPECT_EQ(t1, p1.get());

  auto p2 = cache_get(InputT(2, 2), [t2]() { return t2; });
  EXPECT_EQ(t2, p2.get());

  auto p3 = cache_get(InputT(1, 1), []() { return new CoefficientTable(1, 1); });
  EXPECT_EQ(t1, p3.get());

  // After dropping p1, t1 should still be in the cache.
  p1 = CoefficientTableCache<InputT>::SharedPtr();
  EXPECT_EQ(nullptr, p1.get());

  auto p4 = cache_get(InputT(1, 1), []() { return new CoefficientTable(1, 1); });
  EXPECT_EQ(t1, p4.get());

  // After dropping p3 and p4, t1 should be evicted.
  p3 = CoefficientTableCache<InputT>::SharedPtr();
  p4 = CoefficientTableCache<InputT>::SharedPtr();
  EXPECT_EQ(nullptr, p3.get());
  EXPECT_EQ(nullptr, p4.get());

  CoefficientTable* t5 = new CoefficientTable(1, 1);
  auto p5 = cache_get(InputT(1, 1), [t5]() { return t5; });
  EXPECT_EQ(t5, p5.get());

  // t2 should still be cached.
  auto p6 = cache_get(InputT(2, 2), []() { return new CoefficientTable(1, 1); });
  EXPECT_EQ(t2, p6.get());

  // This should be equivalent to p6 = SharedPtr().
  p2 = std::move(p6);
  EXPECT_EQ(nullptr, p6.get());

  // After dropping p2, t2 should be evicted.
  p2 = CoefficientTableCache<InputT>::SharedPtr();
  EXPECT_EQ(nullptr, p2.get());

  CoefficientTable* t7 = new CoefficientTable(1, 1);
  auto p7 = cache_get(InputT(2, 2), [t7]() { return t7; });
  EXPECT_EQ(t7, p7.get());
}

TEST(LazySharedCoefficientTableTest, LazinessWorks) {
  using InputT = std::pair<int, int>;
  fit::function<CoefficientTable*()> create_table;
  CoefficientTableCache<InputT> cache([&create_table](InputT) { return create_table(); });

  CoefficientTable* t1 = new CoefficientTable(1, 1);
  CoefficientTable* t3 = new CoefficientTable(1, 1);
  CoefficientTable* t4 = new CoefficientTable(1, 1);

  {
    bool created = false;
    create_table = [t1, &created]() {
      created = true;
      return t1;
    };
    LazySharedCoefficientTable<InputT> p1(&cache, InputT(1, 1));
    EXPECT_FALSE(created);
    EXPECT_EQ(t1, p1.get());
    EXPECT_TRUE(created);

    // Should reused the cached table.
    create_table = []() { return new CoefficientTable(1, 1); };
    LazySharedCoefficientTable<InputT> p2(&cache, InputT(1, 1));
    EXPECT_EQ(t1, p2.get());

    // Should not reused the cached table.
    create_table = [t3]() { return t3; };
    LazySharedCoefficientTable<InputT> p3(&cache, InputT(2, 2));
    EXPECT_EQ(t3, p3.get());
  }

  // After p1 and p2 go out-of-scope, the cache entry should be evicted.
  create_table = [t4]() { return t4; };
  LazySharedCoefficientTable<InputT> p4(&cache, InputT(1, 1));
  EXPECT_EQ(t4, p4.get());
}

}  // namespace
}  // namespace media::audio::mixer
