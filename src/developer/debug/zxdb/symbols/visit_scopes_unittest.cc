// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/visit_scopes.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/inheritance_path.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

using VisitLog = std::vector<InheritancePath>;

struct MemberVisitStep {
  bool is_leaf = false;
  uint32_t byte_offset = 0;
  std::string name;

  bool operator==(const MemberVisitStep& other) const {
    return is_leaf == other.is_leaf && byte_offset == other.byte_offset && name == other.name;
  }
};
using MemberVisitLog = std::vector<MemberVisitStep>;  // byte offset, member name.

// Tests VisitClassHierarchy() and VisitDataMembers() for a simple class. This more robsutly tests
// the data member visitation of VisitDataMembers() than the complex hierarchy variant below.
TEST(VisitScopes, NoHierarchy) {
  auto int32_type = MakeInt32Type();

  // class Collection {
  //   const MemberColl member_coll;  // { int32 member_a; int32 member_b, EmptyColl empty }
  //   EmptyColl empty_coll;          // {}
  //   UnionColl union_coll;          // { int32 union; int32 union_b }
  // };
  auto empty_coll = MakeCollectionType(DwarfTag::kStructureType, "EmptyColl", {});

  auto member_coll = MakeCollectionType(
      DwarfTag::kStructureType, "MemberColl",
      {{"member_a", int32_type}, {"member_b", int32_type}, {"empty", empty_coll}});
  auto const_member_coll = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, member_coll);

  auto member_union = MakeCollectionType(DwarfTag::kUnionType, "UnionColl",
                                         {{"union_a", int32_type}, {"union_b", int32_type}});

  auto coll = MakeCollectionType(DwarfTag::kClassType, "Collection",
                                 {{"member_coll", const_member_coll},
                                  {"empty_coll", empty_coll},
                                  {"union_coll", member_union}});

  // Stores the collections and their paths visited.
  VisitLog visited;

  // Visit the hierarchy, it should get called once with the class itself.
  VisitResult result = VisitClassHierarchy(coll.get(), [&visited](const InheritancePath& path) {
    visited.push_back(path);
    return VisitResult::kContinue;
  });
  EXPECT_EQ(VisitResult::kContinue, result);
  VisitLog expected{{{coll}}};
  EXPECT_EQ(expected, visited);

  // Visit the data members.
  MemberVisitLog member_visited;
  result = VisitDataMembers(
      coll.get(), [&member_visited](bool is_leaf, uint32_t offset, const DataMember* member) {
        member_visited.push_back({is_leaf, offset, member->GetAssignedName()});
        return VisitResult::kContinue;
      });
  EXPECT_EQ(VisitResult::kContinue, result);
  // clang-format off
  MemberVisitLog member_expected{
    {false, 0, "member_coll"},
      {true, 0, "member_a"},
      {true, 4, "member_b"},
      {false, 8, "empty"},
      // Note: unlike C, our test symbols lay out empty members with 0 bytes taken.
    {false, 8, "empty_coll"},
    {false, 8, "union_coll"},
      {true, 8, "union_a"},
      {true, 8, "union_b"}
  };
  // clang-format on
  EXPECT_EQ(member_expected, member_visited);
}

// Tests VisitClassHierarchy() and VisitDataMembers() for a complex class hierarchy
TEST(VisitScopes, ComplexHierarchy) {
  auto int32_type = MakeInt32Type();

  auto base1 = MakeCollectionType(DwarfTag::kClassType, "Base1",
                                  {{"base_member1", int32_type}, {"base_member2", int32_type}});
  auto mid1 = MakeCollectionType(DwarfTag::kClassType, "Mid1", {});
  auto mid2 = MakeCollectionType(DwarfTag::kClassType, "Mid2", {});
  auto derived = MakeCollectionType(DwarfTag::kClassType, "Derived", {{"member", int32_type}});

  // Stores the collections and their paths visited.
  VisitLog visited;

  // Complex hierarchy:
  //   base1 -- mid1 --
  //                   \
  //            mid2 ------ derived
  constexpr uint64_t mid1_offset = 8;
  constexpr uint64_t mid2_offset = 0;
  constexpr uint64_t base1_offset = 32;
  auto mid1_inh = fxl::MakeRefCounted<InheritedFrom>(mid1, mid1_offset);
  auto mid2_inh = fxl::MakeRefCounted<InheritedFrom>(mid2, mid2_offset);
  auto base1_inh = fxl::MakeRefCounted<InheritedFrom>(base1, base1_offset);
  derived->set_inherited_from({LazySymbol(mid1_inh), LazySymbol(mid2_inh)});
  mid1->set_inherited_from({LazySymbol(base1_inh)});

  // Visit all of those, they're visited in depth-first-search order (the ordering was most
  // convenient for the implementation, it can be changed in the future if there's a reason for a
  // specific different order).
  visited = VisitLog();
  VisitResult result = VisitClassHierarchy(derived.get(), [&visited](const InheritancePath& path) {
    visited.push_back(path);
    return VisitResult::kContinue;
  });
  EXPECT_EQ(VisitResult::kContinue, result);
  VisitLog expected = VisitLog{{{derived}},
                               {{derived}, {mid1_inh, mid1}},
                               {{derived}, {mid1_inh, mid1}, {base1_inh, base1}},
                               {{derived}, {mid2_inh, mid2}}};
  EXPECT_EQ(expected, visited);

  // Test early termination at mid1.
  visited = VisitLog();
  result = VisitClassHierarchy(derived.get(), [&visited, mid1](const InheritancePath& path) {
    visited.emplace_back(path);
    return path.base() == mid1.get() ? VisitResult::kDone : VisitResult::kContinue;
  });
  EXPECT_EQ(VisitResult::kDone, result);  // Should have found mid1.
  expected = VisitLog{{{derived}}, {{derived}, {mid1_inh, mid1}}};
  EXPECT_EQ(expected, visited);

  // Visit data members.
  MemberVisitLog member_visited;
  result = VisitDataMembers(
      derived.get(), [&member_visited](bool is_leaf, uint32_t offset, const DataMember* member) {
        member_visited.push_back({is_leaf, offset, member->GetAssignedName()});
        return VisitResult::kContinue;
      });
  EXPECT_EQ(VisitResult::kContinue, result);
  MemberVisitLog member_expected{
      {true, 0, "member"},
      // Net offset is the offset of the base class in the derived class.
      {true, mid1_offset + mid2_offset + base1_offset, "base_member1"},
      // The next member is just the next one after the 4-byte base_member1.
      {true, mid1_offset + mid2_offset + base1_offset + 4, "base_member2"}};
  EXPECT_EQ(member_expected, member_visited);
}

}  // namespace zxdb
