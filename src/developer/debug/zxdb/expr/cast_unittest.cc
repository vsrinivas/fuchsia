// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

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
  Err err = CastExprValue(CastType::kImplicit, int32_one, int32_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(int32_one, result);

  // Signed/unsigned conversion, should just copy the bits (end up with
  // 0xffffffff),
  err =
      CastExprValue(CastType::kImplicit, int32_minus_one, uint32_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(uint32_type.get(), result.type());
  EXPECT_EQ(int32_minus_one.data(), result.data());

  // Signed integer promotion.
  err =
      CastExprValue(CastType::kImplicit, int32_minus_one, uint64_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(uint64_type.get(), result.type());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(), result.GetAs<uint64_t>());

  // Integer truncation
  err = CastExprValue(CastType::kImplicit, int32_minus_one, char_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(char_type.get(), result.type());
  EXPECT_EQ(-1, result.GetAs<int8_t>());

  // Zero integer to boolean.
  ExprValue int32_zero(int32_type, {0, 0, 0, 0}, ExprValueSource());
  err = CastExprValue(CastType::kImplicit, int32_zero, bool_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(bool_type.get(), result.type());
  EXPECT_EQ(0, result.GetAs<int8_t>());

  // Nonzero integer to boolean.
  err = CastExprValue(CastType::kImplicit, int32_minus_one, bool_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(bool_type.get(), result.type());
  EXPECT_EQ(1, result.GetAs<int8_t>());

  // Zero floating point to boolean.
  ExprValue float_minus_zero(float_type, {0, 0, 0, 0x80}, ExprValueSource());
  err =
      CastExprValue(CastType::kImplicit, float_minus_zero, bool_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0, result.GetAs<int8_t>());

  // Nonzero floating point to boolean.
  ExprValue float_one_third(float_type, {0xab, 0xaa, 0xaa, 0x3e},
                            ExprValueSource());
  err = CastExprValue(CastType::kImplicit, float_one_third, bool_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1, result.GetAs<int8_t>());

  // Float to signed.
  ExprValue float_minus_two(float_type, {0x00, 0x00, 0x00, 0xc0},
                            ExprValueSource());
  err =
      CastExprValue(CastType::kImplicit, float_minus_two, int32_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-2, result.GetAs<int32_t>());

  // Float to unsigned.
  err =
      CastExprValue(CastType::kImplicit, float_minus_two, uint64_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(static_cast<uint64_t>(-2), result.GetAs<uint64_t>());

  // Floating point promotion.
  err =
      CastExprValue(CastType::kImplicit, float_minus_two, double_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-2.0, result.GetAs<double>());
  ExprValue double_minus_two = result;

  // Floating point truncation.
  err =
      CastExprValue(CastType::kImplicit, double_minus_two, float_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-2.0f, result.GetAs<float>());

  // Pointer to integer with truncation.
  auto ptr_to_double_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(double_type));
  ExprValue ptr_value(ptr_to_double_type,
                      {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12},
                      ExprValueSource());
  err = CastExprValue(CastType::kImplicit, ptr_value, uint32_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0x9abcdef0, result.GetAs<uint32_t>());
  ExprValue big_int_value = result;

  // Integer to pointer with expansion.
  err = CastExprValue(CastType::kImplicit, int32_minus_one, ptr_to_double_type,
                      &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(-1, result.GetAs<int64_t>());
}

TEST(Cast, Reinterpret) {
  auto int32_type = MakeInt32Type();
  auto uint32_type = MakeInt32Type();
  auto int64_type = MakeInt64Type();
  auto uint64_type = MakeInt64Type();
  auto char_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSignedChar, 1, "char");
  auto bool_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeBoolean, 1, "bool");
  auto double_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double");

  auto ptr_to_int32_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(int32_type));
  auto ptr_to_void_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol());

  ExprValue int32_minus_one(int32_type, {0xff, 0xff, 0xff, 0xff},
                            ExprValueSource());
  ExprValue int64_big_num(int32_type, {8, 7, 6, 5, 4, 3, 2, 1},
                          ExprValueSource());
  ExprValue ptr_to_void(ptr_to_void_type, {8, 7, 6, 5, 4, 3, 2, 1},
                        ExprValueSource());
  ExprValue double_pi(double_type,
                      {0x18, 0x2d, 0x44, 0x54, 0xfb, 0x21, 0x09, 0x40},
                      ExprValueSource());

  // Two pointer types: reinterpret_cast<int32_t*>(ptr_to_void);
  ExprValue result;
  Err err = CastExprValue(CastType::kReinterpret, ptr_to_void,
                          ptr_to_int32_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0x0102030405060708u, result.GetAs<uint64_t>());
  EXPECT_EQ(ptr_to_int32_type.get(), result.type());

  // Conversion from int to void*. C++ would prohibit this case because the
  // integer is 32 bits and the pointer is 64, but the debugger allows it.
  // This should not be sign extended.
  err = CastExprValue(CastType::kReinterpret, int32_minus_one, ptr_to_void_type,
                      &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0xffffffffu, result.GetAs<uint64_t>());

  // Truncation of a number. This is also disallowed in C++.
  err =
      CastExprValue(CastType::kReinterpret, int64_big_num, int32_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(4u, result.data().size());
  EXPECT_EQ(0x05060708u, result.GetAs<uint32_t>());

  // Prohibit conversions between a double and a pointer:
  // reinterpret_cast<void*>(3.14159265258979);
  err = CastExprValue(CastType::kReinterpret, double_pi, ptr_to_void_type,
                      &result);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Can't cast from a 'double'.", err.msg());
}

}  // namespace zxdb
