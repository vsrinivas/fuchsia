// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/convert/collection_view.h"

#include <iterator>
#include <set>
#include <unordered_set>
#include <vector>

#include "gtest/gtest.h"

// Add helper to print our types.
namespace testing {
namespace internal {
template <>
std::string GetTypeName<std::vector<uint32_t>>() {
  return "std::vector<uint32_t>";
}
template <>
std::string GetTypeName<std::unordered_set<uint32_t>>() {
  return "std::unordered_set<uint32_t>";
}
template <>
std::string GetTypeName<std::set<uint32_t>>() {
  return "std::set<uint32_t>";
}
}  // namespace internal
}  // namespace testing

namespace convert {
namespace {

template <typename T>
::testing::AssertionResult Equals(const T& collection, size_t begin,
                                  size_t length, CollectionView<T> view) {
  auto it1 = std::next(collection.begin(), begin);
  auto it2 = view.begin();
  for (size_t i = 0; i < length; ++i) {
    if (it1 == collection.end()) {
      return ::testing::AssertionFailure() << "collection is not large enough.";
    }
    if (it2 == view.end()) {
      return ::testing::AssertionFailure() << "view is not large enough.";
    }
  }
  if (*it1 != *it2) {
    return ::testing::AssertionFailure() << "view is incorrect.";
  }
  return ::testing::AssertionSuccess();
}

template <typename T>
class CollectionViewTest : public ::testing::Test {};

using CollectionTypes =
    ::testing::Types<std::vector<uint32_t>, std::unordered_set<uint32_t>,
                     std::set<uint32_t>>;

TYPED_TEST_CASE(CollectionViewTest, CollectionTypes);

TYPED_TEST(CollectionViewTest, Views) {
  TypeParam values = {0, 1, 2, 3, 4, 5};
  CollectionView<TypeParam> view = values;

  EXPECT_TRUE(Equals(values, 0, values.size(), view));
  EXPECT_TRUE(Equals(values, 1, values.size(), view.Tail()));
  EXPECT_TRUE(Equals(values, 1, values.size() - 2,
                     view.SubCollection(1, values.size() - 2)));
  EXPECT_EQ(*std::next(values.begin()), view.Tail()[0]);
}

}  // namespace
}  // namespace convert
