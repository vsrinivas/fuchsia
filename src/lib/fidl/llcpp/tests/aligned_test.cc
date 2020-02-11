// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/aligned.h>

#include "gtest/gtest.h"

TEST(Aligned, Initialization) {
  fidl::aligned<int> x(1);
  EXPECT_EQ(x, 1);

  fidl::aligned<int> y(x);
  EXPECT_EQ(y, 1);
}

TEST(Aligned, Reassignment) {
  fidl::aligned<int> x(1);
  x = 2;
  EXPECT_EQ(x, 2);

  fidl::aligned<int> y(3);
  x = y;
  EXPECT_EQ(x, 3);
}

TEST(Aligned, DefaultConstructor) {
  fidl::aligned<int> x;
  x = 1;
  EXPECT_EQ(x, 1);
}

TEST(Aligned, ImplicitConversion) {
  fidl::aligned<uint64_t> x = int32_t(1);
  int32_t converted_back = x;
  EXPECT_EQ(converted_back, 1);
}

TEST(Aligned, Reference) {
  fidl::aligned<uint32_t> x = int32_t(1);
  EXPECT_EQ(reinterpret_cast<uint32_t*>(&x), &x.value);
}
