// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/function_type.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "garnet/bin/zxdb/symbols/variable.h"
#include "gtest/gtest.h"

namespace zxdb {

// Note: The variants of ComputeFullNameForFunctionPtr() with a parameter are
// tested by the MemberPtr unit test.

TEST(FunctionType, ComputeFullName) {
  // Everything empty. This is the not-technically-valid-C++ case of having
  // a direct reference to a function.
  auto standalone = fxl::MakeRefCounted<FunctionType>(
      LazySymbol(), std::vector<LazySymbol>());
  EXPECT_EQ("void()", standalone->GetFullName());

  // One with args and a return value.
  auto int32_type = MakeInt32Type();
  std::vector<LazySymbol> params{
      LazySymbol(fxl::MakeRefCounted<Variable>(Symbol::kTagFormalParameter, "",
                                               LazySymbol(int32_type),
                                               VariableLocation())),
      LazySymbol(fxl::MakeRefCounted<Variable>(Symbol::kTagFormalParameter, "",
                                               LazySymbol(int32_type),
                                               VariableLocation()))};
  auto with_stuff =
      fxl::MakeRefCounted<FunctionType>(LazySymbol(int32_type), params);
  EXPECT_EQ("int32_t(int32_t, int32_t)", with_stuff->GetFullName());

  // A regular pointer to the functions above.
  auto standalone_ptr = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagPointerType, LazySymbol(standalone));
  EXPECT_EQ("void (*)()", standalone_ptr->GetFullName());

  auto with_stuff_ptr = fxl::MakeRefCounted<ModifiedType>(
      Symbol::kTagPointerType, LazySymbol(with_stuff));
  EXPECT_EQ("int32_t (*)(int32_t, int32_t)", with_stuff_ptr->GetFullName());

  // Member function pointers are tested by the MemberPtr test.
}

}  // namespace zxdb
