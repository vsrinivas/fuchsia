// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/fidl_test.h>

#include "gtest/gtest.h"
#include "lib/fidl/cpp/optional.h"

namespace fidl_test {
namespace {

TEST(FidlTest, SimpleStructEquality) {
  Int64Struct s1{1};
  Int64Struct s2{2};

  EXPECT_EQ(s1, s1);
  EXPECT_EQ(s1, Int64Struct({1}));
  EXPECT_NE(s1, s2);
}

TEST(FidlTest, OptionalFieldEquality) {
  HasOptionalFieldStruct struct_with_null{nullptr};
  HasOptionalFieldStruct struct_with_1{fidl::MakeOptional(Int64Struct({1}))};
  HasOptionalFieldStruct struct_with_2{fidl::MakeOptional(Int64Struct({2}))};

  EXPECT_EQ(struct_with_null, struct_with_null);
  EXPECT_EQ(struct_with_1, struct_with_1);
  EXPECT_NE(struct_with_null, struct_with_1);
  EXPECT_NE(struct_with_1, struct_with_null);
  EXPECT_NE(struct_with_1, struct_with_2);
}

TEST(FidlTest, EqualityOnMultipleFields) {
  Has2OptionalFieldStruct struct1 { nullptr, fidl::MakeOptional(Int64Struct({1}))};
  Has2OptionalFieldStruct struct2 { nullptr, fidl::MakeOptional(Int64Struct({2}))};

  EXPECT_EQ(struct1, struct1);
  EXPECT_NE(struct1, struct2);
}

}  // namespace
}  // namespace fidl_test
