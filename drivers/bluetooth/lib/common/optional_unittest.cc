// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/common/optional.h"

#include "gtest/gtest.h"

namespace bluetooth {
namespace common {
namespace {

TEST(OptionalTest, AssignValue) {
  Optional<int> optional_int;
  EXPECT_FALSE(optional_int.HasValue());
  EXPECT_FALSE(optional_int);
  EXPECT_EQ(nullptr, optional_int.value());

  optional_int = 5;
  EXPECT_TRUE(optional_int.HasValue());
  EXPECT_TRUE(optional_int);
  EXPECT_NE(nullptr, optional_int.value());

  EXPECT_EQ(5, *optional_int.value());
  EXPECT_EQ(5, *optional_int);
}

TEST(OptionalTest, Copy) {
  Optional<int> optional_int1, optional_int2;

  optional_int1 = 5;
  EXPECT_TRUE(optional_int1);
  EXPECT_FALSE(optional_int2);

  optional_int2 = optional_int1;
  EXPECT_TRUE(optional_int1);
  EXPECT_TRUE(optional_int2);
  EXPECT_EQ(5, *optional_int1);
  EXPECT_EQ(5, *optional_int2);
}

TEST(OptionalTest, Reset) {
  Optional<int> optional_int1, optional_int2;
  EXPECT_FALSE(optional_int1);
  EXPECT_FALSE(optional_int2);

  optional_int1 = 5;
  EXPECT_TRUE(optional_int1);
  EXPECT_FALSE(optional_int2);

  // Call Reset() directly.
  optional_int1.Reset();
  EXPECT_FALSE(optional_int1);

  optional_int1 = 5;
  EXPECT_TRUE(optional_int1);

  // Assign from other empty Optional.
  optional_int1 = optional_int2;
  EXPECT_FALSE(optional_int1);
}

TEST(OptionalTest, UniquePtr) {
  Optional<std::unique_ptr<int>> optional_ptr;
  EXPECT_FALSE(optional_ptr);

  optional_ptr = std::make_unique<int>(5);
  EXPECT_TRUE(optional_ptr);
  EXPECT_EQ(5, *(*optional_ptr));

  optional_ptr = std::make_unique<int>(6);
  EXPECT_TRUE(optional_ptr);
  EXPECT_EQ(6, *(*optional_ptr));

  optional_ptr.Reset();
  EXPECT_FALSE(optional_ptr);
}

TEST(OptionalTest, Vector) {
  Optional<std::vector<int>> optional_vector;
  EXPECT_FALSE(optional_vector);

  optional_vector = std::vector<int>{1, 2, 3, 4, 5};
  EXPECT_TRUE(optional_vector);
  EXPECT_EQ(5u, optional_vector->size());

  std::vector<int> vec;
  optional_vector = vec;
  EXPECT_TRUE(optional_vector);
  EXPECT_EQ(0u, optional_vector->size());

  optional_vector.Reset();
  EXPECT_FALSE(optional_vector);
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
