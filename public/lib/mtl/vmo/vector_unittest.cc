// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/vmo/vector.h"
#include "gtest/gtest.h"

namespace mtl {
namespace {

TEST(VmoVector, ShortVector) {
  std::vector<char> v(123, 'f');
  mx::vmo sb;
  EXPECT_TRUE(VmoFromVector(v, &sb));
  std::vector<char> v_out;
  EXPECT_TRUE(VectorFromVmo(std::move(sb), &v_out));
  EXPECT_EQ(v, v_out);
}

TEST(VmoVector, EmptyVector) {
  std::vector<char> v;
  mx::vmo sb;
  EXPECT_TRUE(VmoFromVector(v, &sb));
  std::vector<char> v_out;
  EXPECT_TRUE(VectorFromVmo(std::move(sb), &v_out));
  EXPECT_EQ(v, v_out);
}

}  // namespace
}  // namespace mtl
