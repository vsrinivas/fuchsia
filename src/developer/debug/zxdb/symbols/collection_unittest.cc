// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/collection.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variant_part.h"

namespace zxdb {

TEST(Collection, GetSpecialType) {
  // This DataMember encodes a name of __0 which is used for Rust tuples.
  auto zero_member = fxl::MakeRefCounted<DataMember>("__0", MakeInt32Type(), 0);

  // Regular C struct. Give it a member of "__0" to make sure we're checking the language properly
  // (in this case the language is unset).
  auto regular_c = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "RegularC");
  regular_c->set_data_members({LazySymbol(zero_member)});
  EXPECT_EQ(Collection::kNotSpecial, regular_c->GetSpecialType());

  // A regular Rust structure with no members.
  auto regular_rust = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "RegularRust");
  SymbolTestParentSetter regular_rust_parent(regular_rust, MakeRustUnit());
  EXPECT_EQ(Collection::kNotSpecial, regular_rust->GetSpecialType());

  // A Rust tuple struct which has a normal name and a member named __0.
  auto rust_tuple_struct = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "TupleStruct");
  SymbolTestParentSetter rust_tuple_struct_parent(rust_tuple_struct, MakeRustUnit());
  rust_tuple_struct->set_data_members({LazySymbol(zero_member)});
  EXPECT_EQ(Collection::kRustTupleStruct, rust_tuple_struct->GetSpecialType());

  // A Rust typle which has a name with "(...)" and a member named __0.
  auto rust_tuple = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "(i32, i32)");
  SymbolTestParentSetter rust_tuple_parent(rust_tuple, MakeRustUnit());
  rust_tuple->set_data_members({LazySymbol(zero_member)});
  EXPECT_EQ(Collection::kRustTuple, rust_tuple->GetSpecialType());

  // A Rust Enum has a variant part. This makes a mostly empty one but is
  // // good enough.
  auto rust_enum = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType, "Foo");
  SymbolTestParentSetter rust_enum_parent(rust_enum, MakeRustUnit());
  rust_enum->set_variant_part(
      fxl::MakeRefCounted<VariantPart>(LazySymbol(), std::vector<LazySymbol>()));
  EXPECT_EQ(Collection::kRustEnum, rust_enum->GetSpecialType());
}

}  // namespace zxdb
