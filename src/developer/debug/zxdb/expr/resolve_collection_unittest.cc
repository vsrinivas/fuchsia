// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_collection.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/inherited_from.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/identifier.h"

namespace zxdb {

namespace {

// Defines a class with two member types "a" and "b". It puts the definitions
// of "a" and "b' members into the two out params.
fxl::RefPtr<Collection> GetTestClassType(const DataMember** member_a,
                                         const DataMember** member_b) {
  auto int32_type = MakeInt32Type();
  auto sc = MakeCollectionType(DwarfTag::kStructureType, "Foo",
                               {{"a", int32_type}, {"b", int32_type}});

  *member_a = sc->data_members()[0].Get()->AsDataMember();
  *member_b = sc->data_members()[1].Get()->AsDataMember();
  return sc;
}

// Helper function that calls ResolveMember with an identifier with the
// containing value.
Err ResolveMemberFromString(const ExprValue& base, const std::string& name,
                            ExprValue* out) {
  auto [err, ident] = Identifier::FromString(name);
  if (err.has_error())
    return err;

  return ResolveMember(base, ident, out);
}

}  // namespace

TEST(ResolveCollection, GoodMemberAccess) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Make this const volatile to add extra layers.
  auto vol_sc = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kVolatileType,
                                                  LazySymbol(sc));
  auto const_vol_sc = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType,
                                                        LazySymbol(vol_sc));

  // This struct has the values 1 and 2 in it.
  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(const_vol_sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Resolve A.
  ExprValue out;
  Err err = ResolveMember(base, a_data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(1, out.GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr, out.source().address());

  // Resolve A by name.
  ExprValue out_by_name;
  err = ResolveMemberFromString(base, "a", &out_by_name);
  EXPECT_EQ(out, out_by_name);

  // Resolve B.
  out = ExprValue();
  err = ResolveMember(base, b_data, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(2, out.GetAs<int32_t>());
  EXPECT_EQ(kBaseAddr + 4, out.source().address());

  // Resolve B by name.
  out_by_name = ExprValue();
  err = ResolveMemberFromString(base, "b", &out_by_name);
  EXPECT_EQ(out, out_by_name);
}

TEST(ResolveCollection, BadMemberArgs) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  // Test null base class pointer.
  ExprValue out;
  Err err = ResolveMember(ExprValue(), a_data, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Can't resolve data member on non-struct/class value.", err.msg());

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Null data member pointer.
  out = ExprValue();
  err = ResolveMember(base, nullptr, &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid data member for struct 'Foo'.", err.msg());
}

TEST(ResolveCollection, BadMemberAccess) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto sc = GetTestClassType(&a_data, &b_data);

  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue base(sc, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                 ExprValueSource(kBaseAddr));

  // Lookup by name that doesn't exist.
  ExprValue out;
  Err err = ResolveMemberFromString(base, "c", &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("No member 'c' in struct 'Foo'.", err.msg());

  // Lookup by a DataMember that references outside of the struct (in this
  // case, by one byte).
  auto bad_member = fxl::MakeRefCounted<DataMember>();
  bad_member->set_assigned_name("c");
  bad_member->set_type(LazySymbol(MakeInt32Type()));
  bad_member->set_member_location(5);

  out = ExprValue();
  err = ResolveMember(base, bad_member.get(), &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid data member for struct 'Foo'.", err.msg());
}

// Tests foo.bar where bar is in a derived class of foo's type.
TEST(ResolveCollection, DerivedClass) {
  const DataMember* a_data;
  const DataMember* b_data;
  auto base = GetTestClassType(&a_data, &b_data);

  auto derived = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);

  uint32_t base_offset = 4;  // Offset in derived of base.
  auto inherited =
      fxl::MakeRefCounted<InheritedFrom>(LazySymbol(base), base_offset);
  derived->set_inherited_from({LazySymbol(inherited)});

  // This struct has the values 1 and 2 in it, offset by 4 bytes (the offset
  // within "derived" of "base").
  constexpr uint64_t kBaseAddr = 0x11000;
  ExprValue value(
      derived,
      {0xff, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
      ExprValueSource(kBaseAddr));

  // Resolve B by name.
  ExprValue out;
  Err err = ResolveMemberFromString(value, "b", &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("int32_t", out.type()->GetAssignedName());
  EXPECT_EQ(4u, out.data().size());
  EXPECT_EQ(2, out.GetAs<int32_t>());

  // Offset of B in "derived".
  EXPECT_EQ(kBaseAddr + base_offset + 4, out.source().address());

  // Test extracting the base class from the derived one.
  ExprValue base_value;
  err = ResolveInherited(value, inherited.get(), &base_value);
  EXPECT_FALSE(err.has_error());

  EXPECT_EQ(ExprValue(base, {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00},
                      ExprValueSource(kBaseAddr + base_offset)),
            base_value);
}

}  // namespace zxdb
