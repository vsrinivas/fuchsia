// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/identifier.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

TEST(Identifier, GetName) {
  // Empty.
  Identifier unqualified;
  EXPECT_EQ("", unqualified.GetFullName());

  // Single name with no "::" at the beginning.
  unqualified.AppendComponent("First");
  EXPECT_EQ("First", unqualified.GetFullName());
  std::vector<std::string> expected_index = {"First"};

  // Single name with a "::" at the beginning.
  Identifier qualified(Identifier::kGlobal, "First");
  EXPECT_EQ("::First", qualified.GetFullName());

  // Append some template stuff.
  qualified.AppendComponent("Second", {"int", "Foo"});
  EXPECT_EQ("::First::Second<int, Foo>", qualified.GetFullName());
  expected_index.push_back("Second<int, Foo>");
}

TEST(Identifier, GetScope) {
  std::string name1("Name1");
  std::string name2("Name2");
  std::string name3("Name3");

  // "" -> "".
  Identifier empty;
  EXPECT_EQ("", empty.GetScope().GetDebugName());

  // "::" -> "::".
  Identifier scope_only(Identifier::kGlobal);
  EXPECT_EQ("::", scope_only.GetScope().GetDebugName());

  // "Name1" -> "".
  Identifier name_only(Identifier::kRelative, Identifier::Component(name1));
  EXPECT_EQ("", name_only.GetScope().GetDebugName());

  // ::Name1" -> "::".
  Identifier scoped_name(Identifier::kGlobal, Identifier::Component(name1));
  EXPECT_EQ("::", scoped_name.GetScope().GetDebugName());

  // "Name1::Name2" -> "Name1".
  Identifier two_names(Identifier::kRelative, Identifier::Component(name1));
  two_names.AppendComponent(Identifier::Component(name2));
  EXPECT_EQ("\"Name1\"", two_names.GetScope().GetDebugName());

  // "::Name1::Name2" -> "::Name1".
  Identifier two_scoped_names(Identifier::kGlobal,
                              Identifier::Component(name1));
  two_scoped_names.AppendComponent(Identifier::Component(name2));
  EXPECT_EQ("::\"Name1\"", two_scoped_names.GetScope().GetDebugName());

  // "Name1::Name2::Name3" -> "Name1::Name2".
  Identifier three_scoped_names(Identifier::kRelative,
                                Identifier::Component(name1));
  three_scoped_names.AppendComponent(name2);
  three_scoped_names.AppendComponent(name3);
  EXPECT_EQ("\"Name1\"; ::\"Name2\"",
            three_scoped_names.GetScope().GetDebugName());
}

}  // namespace zxdb
