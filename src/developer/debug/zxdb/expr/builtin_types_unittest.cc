// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(BuiltinTypes, Basic) {
  auto found_long = GetBuiltinType("long");
  ASSERT_TRUE(found_long);
  EXPECT_EQ("long", found_long->GetFullName());
  EXPECT_EQ(BaseType::kBaseTypeSigned, found_long->base_type());
  EXPECT_EQ(8u, found_long->byte_size());

  auto found_double = GetBuiltinType("double");
  ASSERT_TRUE(found_double);
  EXPECT_EQ("double", found_double->GetFullName());
  EXPECT_EQ(BaseType::kBaseTypeFloat, found_double->base_type());
  EXPECT_EQ(8u, found_double->byte_size());

  auto unfound = GetBuiltinType("unfound");
  EXPECT_FALSE(unfound);
}

}  // namespace zxdb
