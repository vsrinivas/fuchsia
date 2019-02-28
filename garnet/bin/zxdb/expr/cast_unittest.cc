// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/cast.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(Cast, Coerce) {
  auto int32_type = MakeInt32Type();
  auto uint32_type = MakeInt32Type();
  auto int64_type = MakeInt64Type();
  auto uint64_type = MakeInt64Type();
  auto char_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char");
  auto bool_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool");
  auto float_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 4, "float");
  auto double_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double");

  ExprValue int32_one(int32_type, {1, 0, 0, 0}, ExprValueSource());
  ExprValue int32_minus_one(int32_type, {0xff, 0xff, 0xff, 0xff},
                            ExprValueSource());

  // Simple identity conversion.
  ExprValue result;
  Err err = CoerceValueTo(int32_one, int32_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(int32_one, result);

  // Signed/unsigned conversion, should just copy the bits (end up with
  // 0xffffffff),
  err = CoerceValueTo(int32_minus_one, uint32_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(uint32_type.get(), result.type());
  EXPECT_EQ(int32_minus_one.data(), result.data());

  // Signed integer promotion.
  err = CoerceValueTo(int32_minus_one, uint64_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(uint64_type.get(), result.type());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(), result.GetAs<uint64_t>());

  // Integer truncation
  err = CoerceValueTo(int32_minus_one, char_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(char_type.get(), result.type());
  EXPECT_EQ(-1, result.GetAs<int8_t>());

  // Zero integer to boolean.
  ExprValue int32_zero(int32_type, {0, 0, 0, 0}, ExprValueSource());
  err = CoerceValueTo(int32_zero, bool_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(bool_type.get(), result.type());
  EXPECT_EQ(0, result.GetAs<int8_t>());

  // Nonzero integer to boolean.
  err = CoerceValueTo(int32_minus_one, bool_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(bool_type.get(), result.type());
  EXPECT_EQ(1, result.GetAs<int8_t>());

  // Zero floating point to boolean.
  ExprValue float_minus_zero(float_type, {0, 0, 0, 0x80}, ExprValueSource());
  err = CoerceValueTo(float_minus_zero, bool_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0, result.GetAs<int8_t>());

  // Nonzero floating point to boolean.
  ExprValue float_one_third(float_type, {0xab, 0xaa, 0xaa, 0x3e},
                            ExprValueSource());
  err = CoerceValueTo(float_one_third, bool_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1, result.GetAs<int8_t>());

  // Float to signed.
  ExprValue float_minus_two(float_type, {0x00, 0x00, 0x00, 0xc0},
                            ExprValueSource());
  err = CoerceValueTo(float_minus_two, int32_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-2, result.GetAs<int32_t>());

  // Float to unsigned.
  err = CoerceValueTo(float_minus_two, uint64_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(static_cast<uint64_t>(-2), result.GetAs<uint64_t>());

  // Floating point promotion.
  err = CoerceValueTo(float_minus_two, double_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-2.0, result.GetAs<double>());
  ExprValue double_minus_two = result;

  // Floating point truncation.
  err = CoerceValueTo(double_minus_two, float_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-2.0f, result.GetAs<float>());

  // Pointer to integer with truncation.
  auto ptr_to_double_type = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagPointerType, LazySymbol(double_type));
  ExprValue ptr_value(ptr_to_double_type,
                      {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12},
                      ExprValueSource());
  err = CoerceValueTo(ptr_value, uint32_type, ExprValueSource(), &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0x9abcdef0, result.GetAs<uint32_t>());
  ExprValue big_int_value = result;

  // Integer to pointer with expansion.
  err = CoerceValueTo(int32_minus_one, ptr_to_double_type, ExprValueSource(),
                      &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-1, result.GetAs<int64_t>());
}

}  // namespace zxdb
