// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/member_ptr.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/function_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

TEST(MemberPtr, Function) {
  // This type is "void (*)()"
  auto standalone = fxl::MakeRefCounted<FunctionType>(
      LazySymbol(), std::vector<LazySymbol>());
  auto standalone_ptr = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(standalone));

  // Class to be the containing class for the pointer to member func.
  auto containing = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  containing->set_assigned_name("MyClass");

  // A parameter type.
  auto int32_type = MakeInt32Type();

  // Make a function pointer ("int32_t (*)(void (*)(), int32_t)". This
  // specified names for the variables which we don't use, but ensures the
  // behavior about named parameters in function pointers is consistent.
  std::vector<LazySymbol> params{
      LazySymbol(fxl::MakeRefCounted<Variable>(
          DwarfTag::kFormalParameter, "val1", LazySymbol(standalone_ptr),
          VariableLocation())),
      LazySymbol(fxl::MakeRefCounted<Variable>(DwarfTag::kFormalParameter,
                                               "val2", LazySymbol(int32_type),
                                               VariableLocation()))};
  auto function =
      fxl::MakeRefCounted<FunctionType>(LazySymbol(int32_type), params);
  auto function_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                        LazySymbol(function));
  EXPECT_EQ("int32_t (*)(void (*)(), int32_t)", function_ptr->GetFullName());

  // Make that function pointer a member pointer.
  auto member_ptr = fxl::MakeRefCounted<MemberPtr>(LazySymbol(containing),
                                                   LazySymbol(function));
  EXPECT_EQ("int32_t (MyClass::*)(void (*)(), int32_t)",
            member_ptr->GetFullName());
}

TEST(MemberPtr, Data) {
  // Class to be the containing class for the pointer to member func.
  auto containing = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  containing->set_assigned_name("MyClass");

  auto int32_type = MakeInt32Type();

  // MyClass member of int.
  auto int_ptr = fxl::MakeRefCounted<MemberPtr>(LazySymbol(containing),
                                                LazySymbol(int32_type));
  EXPECT_EQ("int32_t MyClass::*", int_ptr->GetFullName());

  // MyClass member of MyClass.
  auto class_ptr = fxl::MakeRefCounted<MemberPtr>(LazySymbol(containing),
                                                  LazySymbol(containing));
  EXPECT_EQ("MyClass MyClass::*", class_ptr->GetFullName());
}

}  // namespace zxdb
