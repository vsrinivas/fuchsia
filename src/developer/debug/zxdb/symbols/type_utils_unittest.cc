// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/type_utils.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"

namespace zxdb {

TEST(TypeUtils, GetPointedToType_Null) {
  const Type* pointed_to = nullptr;
  Err err = GetPointedToType(nullptr, &pointed_to);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("No type information.", err.msg());
}

TEST(TypeUtils, GetPointedToType_NotPointer) {
  auto int32_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");

  const Type* pointed_to = nullptr;
  Err err = GetPointedToType(int32_type.get(), &pointed_to);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Attempting to dereference 'int32_t' which is not a pointer.",
            err.msg());
}

TEST(TypeUtils, GetPointedToType_NoPointedToType) {
  // Pointer to nothing.
  auto ptr_type =
      fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, LazySymbol());

  const Type* pointed_to = nullptr;
  Err err = GetPointedToType(ptr_type.get(), &pointed_to);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Missing pointer type info, please file a bug with a repro.",
            err.msg());
}

TEST(TypeUtils, GetPointedToType_Good) {
  auto int32_type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 4, "int32_t");
  auto ptr_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
                                                    LazySymbol(int32_type));

  const Type* pointed_to = nullptr;
  Err err = GetPointedToType(ptr_type.get(), &pointed_to);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(int32_type.get(), pointed_to);
}

}  // namespace zxdb
