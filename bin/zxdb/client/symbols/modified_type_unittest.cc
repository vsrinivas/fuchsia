// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

fxl::RefPtr<BaseType> MakeBaseType(const char* name, int base_type,
                                   uint32_t byte_size) {
  fxl::RefPtr<BaseType> result = fxl::MakeRefCounted<BaseType>();
  result->set_base_type(base_type);
  result->set_byte_size(byte_size);
  result->set_assigned_name(name);
  return result;
}

fxl::RefPtr<ModifiedType> MakeModified(fxl::RefPtr<Type> modified, int tag) {
  fxl::RefPtr<ModifiedType> result = fxl::MakeRefCounted<ModifiedType>(tag);
  result->set_modified(LazySymbol(std::move(modified)));
  return result;
}

}  // namespace

TEST(ModifiedType, GetFullName) {
  // int
  auto int_type = MakeBaseType("int", BaseType::kBaseTypeSigned, 8);
  EXPECT_EQ("int", int_type->GetFullName());

  // int*
  auto int_ptr = MakeModified(int_type, Symbol::kTagPointerType);
  EXPECT_EQ("int*", int_ptr->GetFullName());

  // const int
  auto const_int = MakeModified(int_type, Symbol::kTagConstType);
  EXPECT_EQ("const int", const_int->GetFullName());

  // const int*
  auto const_int_ptr = MakeModified(const_int, Symbol::kTagPointerType);
  EXPECT_EQ("const int*", const_int_ptr->GetFullName());

  // const int* const
  auto const_int_const_ptr = MakeModified(const_int_ptr, Symbol::kTagConstType);
  EXPECT_EQ("const int* const", const_int_const_ptr->GetFullName());

  // const int* const&
  auto const_int_const_ptr_ref =
      MakeModified(const_int_const_ptr, Symbol::kTagReferenceType);
  EXPECT_EQ("const int* const&", const_int_const_ptr_ref->GetFullName());

  // volatile
  auto volatile_int = MakeModified(int_type, Symbol::kTagVolatileType);
  EXPECT_EQ("volatile int", volatile_int->GetFullName());

  // volatile int&&
  auto volatile_int_rvalue_ref =
      MakeModified(volatile_int, Symbol::kTagRvalueReferenceType);
  EXPECT_EQ("volatile int&&", volatile_int_rvalue_ref->GetFullName());

  // typedef const int* Foo
  auto typedef_etc = MakeModified(const_int_ptr, Symbol::kTagTypedef);
  typedef_etc->set_assigned_name("Foo");
  EXPECT_EQ("Foo", typedef_etc->GetFullName());
}

}  // namespace zxdb
