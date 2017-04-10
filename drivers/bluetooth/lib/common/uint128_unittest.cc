// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/common/uint128.h"

#include "gtest/gtest.h"

namespace bluetooth {
namespace common {
namespace {

TEST(UInt128Test, AccessAndComparison) {
  UInt128 foo({0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
               0x0E, 0x0F});
  UInt128 foo_copy(foo);
  UInt128 foo_assigned = foo;

  EXPECT_EQ(foo, foo_copy);
  EXPECT_EQ(foo_copy, foo_assigned);

  foo_copy[0] = 0xFF;
  EXPECT_NE(foo, foo_copy);
  EXPECT_NE(foo_copy, foo_assigned);
  EXPECT_EQ(foo, foo_assigned);

  unsigned int sum = 0;
  for (size_t i = 0; i < sizeof(foo); ++i) {
    sum += foo[i];
  }

  EXPECT_EQ(120u, sum);
}

TEST(UInt128Test, PartialInit) {
  UInt128 zero;
  UInt128 one({0x01});

  EXPECT_NE(zero, one);

  one[0] = 0x00;
  EXPECT_EQ(zero, one);
}

}  // namespace
}  // namespace common
}  // namespace bluetooth
