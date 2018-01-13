// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings2/vector.h"

namespace fidl {
namespace {

TEST(VectorPtr, Control) {
  VectorPtr<int> vector;
  EXPECT_TRUE(vector.is_null());
  EXPECT_FALSE(vector);
  EXPECT_EQ(std::vector<int>(), vector.get());

  std::vector<int> reference = { 1, 2, 3 };

  vector.reset(reference);
  EXPECT_FALSE(vector.is_null());
  EXPECT_TRUE(vector);
  EXPECT_EQ(reference, *vector);
  EXPECT_EQ(3u, vector->size());

  VectorPtr<int> other(std::move(vector));
  EXPECT_EQ(reference, *other);
}

TEST(VectorPtr, PutAt) {
  uint8_t buffer[1024];
  Builder builder(buffer, sizeof(buffer));

  std::vector<int> reference = { 1, 2, 3 };
  VectorPtr<int> vector(reference);

  VectorView<int>* view = builder.New<VectorView<int>>();
  EXPECT_EQ(nullptr, view->data());
  EXPECT_EQ(0u, view->count());
  EXPECT_TRUE(PutAt(&builder, view, &vector));
  EXPECT_EQ(reference, *vector);
  EXPECT_EQ(3u, view->count());
  EXPECT_EQ(0, memcmp(view->data(), vector->data(), 3u));
}

}  // namespace
}  // namespace fidl
