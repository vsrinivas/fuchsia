// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/trace/internal/fields.h"

#include "gtest/gtest.h"

namespace tracing {
namespace internal {
namespace {

TEST(FieldTest, GetSet) {
  uint64_t value(0);

  Field<0, 0>::Set(value, uint8_t(1));
  Field<1, 1>::Set(value, uint8_t(1));
  Field<2, 2>::Set(value, uint8_t(1));
  Field<3, 3>::Set(value, uint8_t(1));
  Field<4, 4>::Set(value, uint8_t(1));
  Field<5, 5>::Set(value, uint8_t(1));
  Field<6, 6>::Set(value, uint8_t(1));
  Field<7, 7>::Set(value, uint8_t(1));

  EXPECT_EQ(uint8_t(-1), value);
  value = 0;
  Field<0, 2>::Set(value, uint8_t(7));
  EXPECT_EQ(uint8_t(7), value);
  Field<0, 2>::Set(value, uint8_t(0));
  EXPECT_EQ(uint8_t(0), value);
}

}  // namespace
}  // namespace internal
}  // namespace tracing
