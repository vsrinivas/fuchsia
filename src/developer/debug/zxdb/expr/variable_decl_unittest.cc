// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/variable_decl.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/abi_null.h"
#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/mock_expr_node.h"
#include "src/developer/debug/zxdb/expr/test_eval_context_impl.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

TEST(VariableDecl, CAutoTypeInfo) {
  auto auto_type = GetBuiltinType(ExprLanguage::kC, "auto");
  ASSERT_TRUE(auto_type);

  // Plain "auto".
  ErrOr<VariableDeclTypeInfo> result = GetVariableDeclTypeInfo(ExprLanguage::kC, auto_type);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kCAuto, result.value().kind);
  EXPECT_EQ(nullptr, result.value().concrete_type.get());

  // "auto&".
  auto auto_ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, auto_type);
  result = GetVariableDeclTypeInfo(ExprLanguage::kC, auto_ref_type);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kCAutoRef, result.value().kind);
  EXPECT_EQ(nullptr, result.value().concrete_type.get());

  // "auto*".
  auto auto_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, auto_type);
  result = GetVariableDeclTypeInfo(ExprLanguage::kC, auto_ptr_type);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kCAutoPtr, result.value().kind);
  EXPECT_EQ(nullptr, result.value().concrete_type.get());

  // "auto**" is not supported.
  auto auto_ptr_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, auto_ptr_type);
  result = GetVariableDeclTypeInfo(ExprLanguage::kC, auto_ptr_ptr_type);
  ASSERT_FALSE(result.ok());
  const char kAutoErrMsg[] =
      "Only 'auto', 'auto*' and 'auto&' variable types are supported in the debugger.";
  EXPECT_EQ(kAutoErrMsg, result.err().msg());

  // "auto&*" is not supported.
  auto auto_ref_ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, auto_ref_type);
  result = GetVariableDeclTypeInfo(ExprLanguage::kC, auto_ref_ptr_type);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(kAutoErrMsg, result.err().msg());

  // Null type means "auto".
  result = GetVariableDeclTypeInfo(ExprLanguage::kC, nullptr);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kCAuto, result.value().kind);
  EXPECT_EQ(nullptr, result.value().concrete_type.get());
}

TEST(VariableDecl, RustAutoTypeInfo) {
  // Rust auto variable declarations use only "null" types.
  ErrOr<VariableDeclTypeInfo> result = GetVariableDeclTypeInfo(ExprLanguage::kRust, nullptr);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kRustAuto, result.value().kind);
  EXPECT_EQ(nullptr, result.value().concrete_type.get());

  // "auto" is not a type name in Rust, it will be treated as the name of a normal type.  This uses
  // the "C" auto builtin type to get something named "auto" (the language is not encoded in the
  // resulting type.
  auto auto_type = GetBuiltinType(ExprLanguage::kC, "auto");
  ASSERT_TRUE(auto_type);
  result = GetVariableDeclTypeInfo(ExprLanguage::kRust, auto_type);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kExplicit, result.value().kind);
  EXPECT_EQ(auto_type.get(), result.value().concrete_type.get());
}

TEST(VariableDecl, ExplicitType) {
  auto type = MakeCollectionType(DwarfTag::kStructureType, "Type", {});
  auto type_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, type);

  // Explicit type should just get copied back.
  ErrOr<VariableDeclTypeInfo> result = GetVariableDeclTypeInfo(ExprLanguage::kC, type_ptr);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kExplicit, result.value().kind);
  EXPECT_EQ(type_ptr.get(), result.value().concrete_type.get());

  // Same thing with the Rust flag.
  result = GetVariableDeclTypeInfo(ExprLanguage::kRust, type_ptr);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(VariableDeclTypeInfo::kExplicit, result.value().kind);
  EXPECT_EQ(type_ptr.get(), result.value().concrete_type.get());
}

// EmitVariableInitializerOps is tested in eval_unittest.cc as part of the test of local variable
// integration.

}  // namespace zxdb
