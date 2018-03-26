// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/vector.h"
#include "gtest/gtest.h"

namespace fidl {
namespace {

TEST(VectorPtr, Control) {
  VectorPtr<int> vector;
  EXPECT_TRUE(vector.is_null());
  EXPECT_FALSE(vector);
  vector->push_back(1);
  EXPECT_FALSE(vector.is_null());

  std::vector<int> reference = {1, 2, 3};

  vector.reset(reference);
  EXPECT_FALSE(vector.is_null());
  EXPECT_TRUE(vector);
  EXPECT_EQ(reference, vector.get());
  EXPECT_EQ(reference, *vector);
  EXPECT_EQ(3u, vector->size());

  VectorPtr<int> other(std::move(vector));
  EXPECT_EQ(reference, *other);

  std::vector<int> taken = other.take();
  EXPECT_TRUE(other.is_null());
  EXPECT_EQ(3u, taken.size());

  VectorPtr<int> sized(3);
  EXPECT_FALSE(sized.is_null());
  EXPECT_TRUE(sized);
  EXPECT_EQ(3u, sized->size());
  EXPECT_EQ(0, sized->at(0));
}

}  // namespace
}  // namespace fidl
