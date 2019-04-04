// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/cast.h"
#include "src/developer/debug/zxdb/expr/eval_test_support.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

TEST(Cast, Implicit) {
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

  ExprValue int32_one(int32_type, {1, 0, 0, 0});
  ExprValue int32_minus_one(int32_type, {0xff, 0xff, 0xff, 0xff});

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
  ExprValue int32_zero(int32_type, {0, 0, 0, 0});
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
  ExprValue float_minus_zero(float_type, {0, 0, 0, 0x80});
  err =
      CastExprValue(CastType::kImplicit, float_minus_zero, bool_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0, result.GetAs<int8_t>());

  // Nonzero floating point to boolean.
  ExprValue float_one_third(float_type, {0xab, 0xaa, 0xaa, 0x3e});
  err = CastExprValue(CastType::kImplicit, float_one_third, bool_type, &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1, result.GetAs<int8_t>());

  // Float to signed.
  ExprValue float_minus_two(float_type, {0x00, 0x00, 0x00, 0xc0});
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
                      {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12});
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

// Tests implicit casting when there are derived classes.
TEST(Cast, ImplicitDerived) {
  DerivedClassTestSetup d;

  // Should be able to implicit cast Derived to Base1 object.
  ExprValue result;
  Err err = CastExprValue(CastType::kImplicit, d.derived_value, d.base1_type,
                          &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(d.base1_type.get(), result.type());
  EXPECT_EQ(d.base1_value, result);
  // Check the source explicitly since == doesn't check sources.
  EXPECT_EQ(d.base1_value.source(), result.source());

  // Same for base 2.
  err = CastExprValue(CastType::kImplicit, d.derived_value, d.base2_type,
                      &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(d.base2_type.get(), result.type());
  EXPECT_EQ(d.base2_value, result);
  // Check the source explicitly since == doesn't check sources.
  EXPECT_EQ(d.base2_value.source(), result.source());

  // Should not be able to implicit cast from Base2 to Derived.
  result = ExprValue();
  err = CastExprValue(CastType::kImplicit, d.base2_value, d.derived_type,
                      &result);
  EXPECT_TRUE(err.has_error());

  // Pointer casting: should be able to implicit cast derived ptr to base ptr
  // type. This data matches kDerivedAddr in 64-bit little endian.
  err = CastExprValue(CastType::kImplicit, d.derived_ptr_value,
                      d.base2_ptr_type, &result);
  EXPECT_FALSE(err.has_error()) << err.msg();

  // That should have adjusted the pointer value appropriately.
  EXPECT_EQ(d.base2_ptr_value, result);

  // Should not allow implicit casts from Base to Derived.
  err = CastExprValue(CastType::kImplicit, d.base2_ptr_value,
                      d.derived_ptr_type, &result);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Can't convert 'Base2*' to unrelated type 'Derived*'.", err.msg());

  // Cast a derived to void* is allowed, just a numeric copy.
  auto void_ptr_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol());
  err = CastExprValue(CastType::kImplicit, d.derived_ptr_value, void_ptr_type,
                      &result);
  EXPECT_FALSE(err.has_error()) << err.msg();
  ExprValue void_ptr_value(void_ptr_type, d.derived_ptr_value.data());
  EXPECT_EQ(void_ptr_value, result);

  // Cast void* to any pointer type (in C this would require an explicit cast).
  err = CastExprValue(CastType::kImplicit, void_ptr_value, d.derived_ptr_type,
                      &result);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(d.derived_ptr_value, result);
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

  ExprValue int32_minus_one(int32_type, {0xff, 0xff, 0xff, 0xff});
  ExprValue int64_big_num(int32_type, {8, 7, 6, 5, 4, 3, 2, 1});
  ExprValue ptr_to_void(ptr_to_void_type, {8, 7, 6, 5, 4, 3, 2, 1});
  ExprValue double_pi(double_type,
                      {0x18, 0x2d, 0x44, 0x54, 0xfb, 0x21, 0x09, 0x40});

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

// Static cast is mostly implement as implicit cast. This only tests the
// additional behavior.
TEST(Cast, Static) {
  DerivedClassTestSetup d;

  // Should NOT be able to static cast from Base2 to Derived.
  // This is "static_cast<Derived>(base);"
  ExprValue result;
  Err err =
      CastExprValue(CastType::kStatic, d.base2_value, d.derived_type, &result);
  EXPECT_TRUE(err.has_error());

  // Cast a derived class reference to a base class reference.
  // This is "static_cast<Base&>(derived_reference);
  err = CastExprValue(CastType::kStatic, d.derived_ref_value, d.base2_ref_type,
                      &result);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(d.base2_ref_value, result);

  // Should be able to go from base->derived with pointers.
  // This is "static_cast<Derived*>(&base);"
  err = CastExprValue(CastType::kStatic, d.base2_ptr_value, d.derived_ptr_type,
                      &result);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(d.derived_ptr_value, result);

  // Base->derived conversion for references.
  err = CastExprValue(CastType::kStatic, d.base2_ref_value, d.derived_ref_type,
                      &result);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(d.derived_ref_value, result);

  // Allow conversion of rvalue references and regular references.
  auto derived_rvalue_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kRvalueReferenceType, LazySymbol(d.derived_type));
  ExprValue derived_rvalue_value(derived_rvalue_type,
                                 d.derived_ref_value.data());
  err = CastExprValue(CastType::kStatic, derived_rvalue_value, d.base2_ref_type,
                      &result);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(d.base2_ref_value, result);

  // Don't allow reference->pointer or pointer->reference casts.
  err = CastExprValue(CastType::kStatic, d.derived_ref_value,
                      d.derived_ptr_type, &result);
  EXPECT_TRUE(err.has_error());
  err = CastExprValue(CastType::kStatic, d.derived_ptr_value,
                      d.derived_ref_type, &result);
  EXPECT_TRUE(err.has_error());
}

TEST(Cast, C) {
  DerivedClassTestSetup d;

  // A C-style cast should be like a static cast when casting between
  // related types.
  ExprValue result;
  Err err = CastExprValue(CastType::kC, d.derived_ref_value, d.base2_ref_type,
                          &result);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(d.base2_ref_value, result);

  // When there are unrelated pointers, it should fall back to reinterpret.
  auto double_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeFloat, 8, "double");
  auto ptr_to_double_type = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(double_type));
  ExprValue ptr_value(ptr_to_double_type,
                      {0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12});
  err = CastExprValue(CastType::kC, ptr_value, d.base2_ptr_type, &result);
  EXPECT_FALSE(err.has_error());

  // For the reinterpret cast, the type should be the new one, with the data
  // being the original.
  EXPECT_EQ(d.base2_ptr_type.get(), result.type());
  EXPECT_EQ(ptr_value.data(), result.data());

  // Can't cast from a ptr to a ref.
  err = CastExprValue(CastType::kC, ptr_value, d.base2_ref_type, &result);
  EXPECT_TRUE(err.has_error());
}

}  // namespace zxdb
