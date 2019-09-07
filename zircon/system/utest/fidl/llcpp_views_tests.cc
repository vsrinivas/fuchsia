// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>
#include <vector>

#include <lib/fidl/llcpp/vector_view.h>
#include <zxtest/zxtest.h>

TEST(VectorView, AdaptorTest) {
  std::vector<uint32_t> vector({1, 2, 3});
  fidl::VectorView view(vector);
  static_assert(std::is_same_v<decltype(view.data()), const uint32_t*>);
  EXPECT_FALSE(view.empty());
  EXPECT_EQ(view.data(), vector.data());
  EXPECT_EQ(view.count(), vector.size());

  // Compile-time tests for fidl::VectorView constructor
  std::vector<const uint32_t> const_vec;
  fidl::VectorView const_view(const_vec);
  static_assert(std::is_same_v<decltype(const_view.mutable_data()), const uint32_t*>);
}
