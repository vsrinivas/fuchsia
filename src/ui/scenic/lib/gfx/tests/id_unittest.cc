// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/id.h"

#include "gtest/gtest.h"

using scenic_impl::GlobalId;

TEST(GlobalId, Equality) {
  EXPECT_TRUE(GlobalId(1, 2) == GlobalId(1, 2));
  EXPECT_FALSE(GlobalId(2, 2) == GlobalId(1, 2));
  EXPECT_FALSE(GlobalId(1, 3) == GlobalId(1, 2));
}

TEST(GlobalId, Inequality) {
  EXPECT_FALSE(GlobalId(1, 2) != GlobalId(1, 2));
  EXPECT_TRUE(GlobalId(2, 2) != GlobalId(1, 2));
  EXPECT_TRUE(GlobalId(1, 3) != GlobalId(1, 2));
}

TEST(GlobalId, LessThan) {
  EXPECT_FALSE(GlobalId(3, 3) < GlobalId(3, 3));
  EXPECT_FALSE(GlobalId(3, 3) < GlobalId(3, 2));
  EXPECT_FALSE(GlobalId(3, 3) < GlobalId(2, 3));
  EXPECT_FALSE(GlobalId(3, 3) < GlobalId(2, 2));

  EXPECT_TRUE(GlobalId(3, 3) < GlobalId(3, 4));
  EXPECT_TRUE(GlobalId(3, 3) < GlobalId(4, 3));
  EXPECT_TRUE(GlobalId(3, 3) < GlobalId(4, 4));
}

TEST(GlobalId, AsString) {
  const GlobalId kId(5,6);
  const std::string kExpected("5-6");

  EXPECT_EQ(kExpected, static_cast<std::string>(kId));

  std::ostringstream out;
  out << kId;
  EXPECT_EQ(kExpected, out.str());
}
