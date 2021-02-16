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
  DEFINE_HARD_INT(DogId, uint64_t);
  DEFINE_HARD_INT(CatId, uint64_t);
  static_assert(!std::is_same<DogId, CatId>::value);
}

TEST(HardIntTest, TwoUintsOfDifferentSizeDontConvert) {
  DEFINE_HARD_INT(DogId, uint32_t);
  DEFINE_HARD_INT(CatId, uint64_t);
  static_assert(!std::is_same<DogId, CatId>::value);
}

TEST(HardIntTest, SameTypesWork) {
  DEFINE_HARD_INT(DogId, uint32_t);

  DogId a1(1), a2(1);
  DogId b(2);
  ASSERT_EQ(a1, a2);
  ASSERT_NE(a1, b);
  ASSERT_NE(a1.value(), b.value());
  static_assert(DogId(1) < DogId(2));
  ASSERT_LT(a1, b);
  b = a1;
  ASSERT_EQ(a1, b);
  std::swap(a1, b);
  ASSERT_EQ(a1, b);
}

TEST(HardIntTest, OrderedContainers) {
  DEFINE_HARD_INT(DogId, uint32_t);

  std::map<int, DogId> dogs;

  dogs.insert({2, DogId(2)});
  dogs.insert({1, DogId(1)});
  EXPECT_EQ(dogs.at(1), DogId(1));
  EXPECT_EQ(dogs.begin()->second, DogId(1));  // Ordering preserved

  std::set<DogId> dog_set;
  dog_set.insert(DogId(2));
  dog_set.insert(DogId(1));
  EXPECT_EQ(*dog_set.begin(), DogId(1));  // Ordering preserved
}

}  // anonymous namespace
