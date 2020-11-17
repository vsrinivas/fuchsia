// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/vector.h"

#include <zxtest/zxtest.h>

namespace fidl {
namespace {

TEST(VectorPtr, Control) {
  VectorPtr<int> vector;
  EXPECT_FALSE(vector.has_value());
  EXPECT_FALSE(vector);

  std::vector<int> reference = {1, 2, 3};

  vector = reference;
  EXPECT_TRUE(vector.has_value());
  EXPECT_TRUE(vector);
  EXPECT_EQ(reference, vector.value());
  EXPECT_EQ(reference, *vector);
  EXPECT_EQ(3u, vector->size());

  VectorPtr<int> other(std::move(vector));
  EXPECT_EQ(reference, *other);

  std::vector<int> taken = std::move(other).value();
  other.reset();
  EXPECT_FALSE(other.has_value());
  EXPECT_EQ(3u, taken.size());

  VectorPtr<int> sized(3);
  EXPECT_TRUE(sized.has_value());
  EXPECT_TRUE(sized);
  EXPECT_EQ(3u, sized->size());
  EXPECT_EQ(0, sized->at(0));
}

TEST(VectorPtr, ResetMoveOnlyType) {
  std::vector<std::unique_ptr<int>> original;
  // can't use initializer list on a move-only type...
  original.push_back(std::make_unique<int>(1));
  original.push_back(std::make_unique<int>(2));
  original.push_back(std::make_unique<int>(3));
  VectorPtr<std::unique_ptr<int>> vector;
  vector = std::move(original);
  EXPECT_TRUE(vector.has_value());
  EXPECT_TRUE(vector);
  EXPECT_EQ(1, *vector->at(0));
  EXPECT_EQ(2, *vector->at(1));
  EXPECT_EQ(3, *vector->at(2));
  EXPECT_EQ(3u, vector->size());
}

TEST(VectorPtr, Constructors) {
  {
    std::vector<int> numbers({1, 2, 3, 4});
    VectorPtr<int> ptr(numbers);
    EXPECT_EQ(numbers.size(), 4u);
    EXPECT_EQ(ptr->size(), 4u);
  }
  {
    std::vector<int> numbers({1, 2, 3, 4});
    VectorPtr<int> ptr(std::move(numbers));
    EXPECT_EQ(numbers.size(), 0u);
    EXPECT_EQ(ptr->size(), 4u);
  }

  {
    VectorPtr<int> ptr1({1, 2, 3, 4});
    VectorPtr<int> ptr2(std::move(ptr1));

    EXPECT_EQ(ptr1->size(), 0u);
    EXPECT_EQ(ptr2->size(), 4u);
  }
}

TEST(VectorPtr, Assignment) {
  VectorPtr<int> ptr1({1, 2, 3, 4});
  VectorPtr<int> ptr2;

  ptr2 = std::move(ptr1);

  EXPECT_EQ(ptr1->size(), 0u);
  EXPECT_EQ(ptr2->size(), 4u);
}

TEST(VectorPtr, StdVectorCopyable) {
  {
    // Copy in constructor
    std::vector<int> vec({1, 2, 3, 4});
    VectorPtr<int> vecptr(vec);
    EXPECT_EQ(vec.size(), 4u);
    EXPECT_EQ(vecptr->size(), 4u);
  }
  {
    // Move in constructor
    std::vector<int> vec({1, 2, 3, 4});
    VectorPtr<int> vecptr(std::move(vec));
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vecptr->size(), 4u);
  }
  {
    // Copy in assignment
    std::vector<int> vec({1, 2, 3, 4});
    VectorPtr<int> vecptr;
    vecptr = vec;
    EXPECT_EQ(vec.size(), 4u);
    EXPECT_EQ(vecptr->size(), 4u);
  }
  {
    // Move in assignment
    std::vector<int> vec({1, 2, 3, 4});
    VectorPtr<int> vecptr;
    vecptr = std::move(vec);
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vecptr->size(), 4u);
  }
}
TEST(VectorPtr, StdVectorMoveOnly) {
  auto mkvec = []() {
    std::vector<std::unique_ptr<int>> vec;
    vec.push_back(std::make_unique<int>(1));
    vec.push_back(std::make_unique<int>(2));
    vec.push_back(std::make_unique<int>(3));
    vec.push_back(std::make_unique<int>(4));
    return vec;
  };
  {
    // Move in constructor
    std::vector<std::unique_ptr<int>> vec = mkvec();
    VectorPtr<std::unique_ptr<int>> vecptr(std::move(vec));
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vecptr->size(), 4u);
  }
  {
    // Move in assignment
    std::vector<std::unique_ptr<int>> vec = mkvec();
    VectorPtr<std::unique_ptr<int>> vecptr;
    vecptr = std::move(vec);
    EXPECT_EQ(vec.size(), 0u);
    EXPECT_EQ(vecptr->size(), 4u);
  }
}

TEST(VectorPtr, FitOptional) {
  std::vector<int> numbers({1, 2, 3, 4});
  VectorPtr<int> a(numbers);
  EXPECT_TRUE(a.has_value());
  EXPECT_TRUE(a);
  EXPECT_FALSE(a->empty());
  EXPECT_EQ(a->size(), 4u);
  EXPECT_EQ(*a, std::vector<int>({1, 2, 3, 4}));
  EXPECT_EQ(a.value(), std::vector<int>({1, 2, 3, 4}));
  EXPECT_EQ(a.value_or(std::vector<int>({1, 2})), std::vector<int>({1, 2, 3, 4}));

  numbers.push_back(5);
  EXPECT_EQ(a->size(), 4u);
  a.value().push_back(5);
  EXPECT_EQ(a->size(), 5u);

  a.reset();
  EXPECT_FALSE(a.has_value());
  EXPECT_EQ(a.value_or(std::vector<int>({1, 2})), std::vector<int>({1, 2}));
}

}  // namespace
}  // namespace fidl
