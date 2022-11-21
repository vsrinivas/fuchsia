// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_variant.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variant.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"

namespace zxdb {

// Tests a variant with two possible values represented by discriminants 0 and
// 1.
TEST(ResolveVariant, TwoValues) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // Random collection to serve as the enclosing struct.
  auto coll = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "Foo");

  auto a = fxl::MakeRefCounted<Variant>(0, std::vector<LazySymbol>{});
  auto b = fxl::MakeRefCounted<Variant>(1, std::vector<LazySymbol>{});

  // 8-bit discriminant.
  auto u8_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "u8");
  auto discr = fxl::MakeRefCounted<DataMember>(std::string(), u8_type, 0);

  auto rust_enum = MakeRustEnum("RustEnum", discr, {a, b});
  auto variant_part = rust_enum->variant_part().Get()->As<VariantPart>();

  // A value.
  ExprValue a_value(rust_enum, {0});
  fxl::RefPtr<Variant> output;
  Err err = ResolveVariant(eval_context, a_value, coll.get(), variant_part, &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(a.get(), output.get());

  // B value.
  ExprValue b_value(rust_enum, {1});
  err = ResolveVariant(eval_context, b_value, coll.get(), variant_part, &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(b.get(), output.get());

  // Invalid value.
  ExprValue invalid_value(rust_enum, {2});
  err = ResolveVariant(eval_context, invalid_value, coll.get(), variant_part, &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(err.msg(), "Discriminant value of 0x2 does not match any of the Variants.");
}

TEST(ResolveVariant, DefaultValue) {
  auto eval_context = fxl::MakeRefCounted<MockEvalContext>();

  // This makes the equivalent in Rust of:
  //
  //   enum RustEnum {
  //     a(),
  //     b(i32),
  //   }
  //
  // Our "B" value is encoded as an int, but in practice Rust will put the int inside a wrapper
  // struct.
  //
  // Here b is encoded with no discriminant value, making it the default. The data is stored at
  // offset 4 in the structure to leave room for the discriminant at offset 0 (padded to 32 bits).
  auto a_enum_type = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "a");  // Empty.
  auto a = fxl::MakeRefCounted<Variant>(
      0, std::vector<LazySymbol>{fxl::MakeRefCounted<DataMember>("a", a_enum_type, 4)});
  auto int32_type = MakeInt32Type();
  auto b = fxl::MakeRefCounted<Variant>(
      std::nullopt, std::vector<LazySymbol>{fxl::MakeRefCounted<DataMember>("b", int32_type, 4)});

  // 8-bit discriminant.
  auto u8_type = fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 1, "u8");
  auto discr = fxl::MakeRefCounted<DataMember>(std::string(), u8_type, 0);

  fxl::RefPtr<Collection> rust_enum = MakeRustEnum("RustEnum", discr, {a, b});
  auto variant_part = rust_enum->variant_part().Get()->As<VariantPart>();

  // A value (4 bytes padded discrminant, 4 byte padded for the other enum's value).
  ExprValue a_value(rust_enum, {0, 0, 0, 0, 0, 0, 0, 0});
  fxl::RefPtr<Variant> output;
  Err err = ResolveVariant(eval_context, a_value, rust_enum.get(), variant_part, &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(a.get(), output.get());

  // Extract the A value.
  ErrOrValue resolved = ResolveSingleVariantValue(eval_context, a_value);
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved.value(), ExprValue());

  // Extract the A name.
  ErrOr<std::string> name = GetActiveRustVariantName(eval_context, a_value);
  ASSERT_TRUE(name.ok());
  EXPECT_EQ("a", name.value());

  // Any other value should give B (4-byte padded discriminant, 4-byte int32 = "42").
  ExprValue b_value(rust_enum, {99, 0, 0, 0, 42, 0, 0, 0});
  err = ResolveVariant(eval_context, b_value, rust_enum.get(), variant_part, &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(b.get(), output.get());

  // Extract the B value.
  resolved = ResolveSingleVariantValue(eval_context, b_value);
  ASSERT_TRUE(resolved.ok());
  EXPECT_EQ(resolved.value(), ExprValue(42, int32_type));

  // Extract the B name.
  name = GetActiveRustVariantName(eval_context, b_value);
  ASSERT_TRUE(name.ok());
  EXPECT_EQ("b", name.value());
}

}  // namespace zxdb
