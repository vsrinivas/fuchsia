// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/shared_buffer/vector.h"
#include "gtest/gtest.h"

namespace mtl {
namespace {

TEST(SharedBufferVector, ShortVector) {
  std::vector<char> v(123, 'f');
  mojo::ScopedSharedBufferHandle sb;
  EXPECT_TRUE(SharedBufferFromVector(v, &sb));
  std::vector<char> v_out;
  EXPECT_TRUE(VectorFromSharedBuffer(std::move(sb), &v_out));
  EXPECT_EQ(v, v_out);
}

TEST(SharedBufferVector, EmptyVector) {
  std::vector<char> v;
  mojo::ScopedSharedBufferHandle sb;
  EXPECT_TRUE(SharedBufferFromVector(v, &sb));
  std::vector<char> v_out;
  EXPECT_TRUE(VectorFromSharedBuffer(std::move(sb), &v_out));
  EXPECT_EQ(v, v_out);
}

}  // namespace
}  // namespace mtl
