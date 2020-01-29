// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <map>
#include <set>
#include <type_traits>

#include <fbl/hard_int.h>
#include <zxtest/zxtest.h>

namespace {

TEST(HardIntTest, TwoUint64DontConvert) {
  DEFINE_HARD_INT(Celsius, uint64_t);
  DEFINE_HARD_INT(Fahrenheit, uint64_t);
  static_assert(std::is_same<Celsius, Fahrenheit>::value == false, "");
}

TEST(HardIntTest, TwoUintsOfDifferentSizeDontConvert) {
  DEFINE_HARD_INT(Kelvin, uint32_t);
  DEFINE_HARD_INT(Rankine, uint64_t);
  static_assert(std::is_same<Kelvin, Rankine>::value == false, "");
}

TEST(HardIntTest, SameTypesWork) {
  DEFINE_HARD_INT(Feet, uint32_t);

  Feet near(1), nearer(1);
  Feet far(2);
  ASSERT_EQ(near, nearer);
  ASSERT_NE(near, far);
  ASSERT_NE(near.value(), far.value());
  static_assert(Feet(1) < Feet(2), "");
  ASSERT_LT(near, far);
  near = far;
  ASSERT_EQ(near, far);
  std::swap(near, far);
  ASSERT_EQ(near, far);
}

TEST(HardIntTest, OrderedContainers) {
  DEFINE_HARD_INT(Feet, uint32_t);

  std::map<int, Feet> footmap;

  footmap.insert({2, Feet(2)});
  footmap.insert({1, Feet(1)});
  EXPECT_EQ(footmap.at(1), Feet(1));
  EXPECT_EQ(footmap.begin()->second, Feet(1));  // Ordering preserved

  std::set<Feet> footset;
  footset.insert(Feet(2));
  footset.insert(Feet(1));
  EXPECT_EQ(*footset.begin(), Feet(1));  // Ordering preserved
}

}  // anonymous namespace
