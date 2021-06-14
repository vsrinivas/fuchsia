// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/builtin_types.h"

#include <gtest/gtest.h>

namespace zxdb {

TEST(BuiltinTypes, C) {
  auto found_long = GetBuiltinType(ExprLanguage::kC, "long");
  ASSERT_TRUE(found_long);
  EXPECT_EQ("long", found_long->GetFullName());
  EXPECT_EQ(BaseType::kBaseTypeSigned, found_long->base_type());
  EXPECT_EQ(8u, found_long->byte_size());

  auto found_double = GetBuiltinType(ExprLanguage::kC, "double");
  ASSERT_TRUE(found_double);
  EXPECT_EQ("double", found_double->GetFullName());
  EXPECT_EQ(BaseType::kBaseTypeFloat, found_double->base_type());
  EXPECT_EQ(8u, found_double->byte_size());

  auto unfound = GetBuiltinType(ExprLanguage::kC, "unfound");
  EXPECT_FALSE(unfound);

  EXPECT_EQ("uint16_t", GetBuiltinUnsignedType(ExprLanguage::kC, 2)->GetFullName());
  EXPECT_EQ("nonstandard_unsigned", GetBuiltinUnsignedType(ExprLanguage::kC, 7)->GetFullName());
  EXPECT_EQ("double", GetBuiltinFloatType(ExprLanguage::kC, 8)->GetFullName());
  EXPECT_EQ("nonstandard_float", GetBuiltinFloatType(ExprLanguage::kC, 7)->GetFullName());
}

TEST(BuiltinTypes, Rust) {
  auto found_i64 = GetBuiltinType(ExprLanguage::kRust, "i64");
  ASSERT_TRUE(found_i64);
  EXPECT_EQ("i64", found_i64->GetFullName());
  EXPECT_EQ(BaseType::kBaseTypeSigned, found_i64->base_type());
  EXPECT_EQ(8u, found_i64->byte_size());

  auto found_f32 = GetBuiltinType(ExprLanguage::kRust, "f32");
  ASSERT_TRUE(found_f32);
  EXPECT_EQ("f32", found_f32->GetFullName());
  EXPECT_EQ(BaseType::kBaseTypeFloat, found_f32->base_type());
  EXPECT_EQ(4u, found_f32->byte_size());

  EXPECT_EQ("u16", GetBuiltinUnsignedType(ExprLanguage::kRust, 2)->GetFullName());
  EXPECT_EQ("nonstandard_unsigned", GetBuiltinUnsignedType(ExprLanguage::kRust, 7)->GetFullName());
  EXPECT_EQ("f64", GetBuiltinFloatType(ExprLanguage::kRust, 8)->GetFullName());
  EXPECT_EQ("nonstandard_float", GetBuiltinFloatType(ExprLanguage::kRust, 7)->GetFullName());
}

}  // namespace zxdb
