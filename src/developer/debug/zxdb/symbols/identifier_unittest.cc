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
  unqualified.AppendComponent(IdentifierComponent("First"));
  EXPECT_EQ("First", unqualified.GetFullName());
  std::vector<std::string> expected_index = {"First"};

  // Single name with a "::" at the beginning.
  Identifier qualified(IdentifierQualification::kGlobal, IdentifierComponent("First"));
  EXPECT_EQ("::First", qualified.GetFullName());

  // Append some template stuff (not parsed in any way).
  qualified.AppendComponent(IdentifierComponent("Second<int, Foo>"));
  EXPECT_EQ("::First::Second<int, Foo>", qualified.GetFullName());

  // One with an anoynymous name in it.
  Identifier anon(IdentifierQualification::kGlobal, IdentifierComponent(std::string()));
  EXPECT_EQ("::$anon", anon.GetFullName());
  anon.AppendComponent(IdentifierComponent("SomeFunction"));
  EXPECT_EQ("::$anon::SomeFunction", anon.GetFullName());

  // PLT function.
  Identifier plt(IdentifierQualification::kRelative,
                 IdentifierComponent(SpecialIdentifier::kPlt, "zx_foo_bar"));
  EXPECT_EQ("$plt(zx_foo_bar)", plt.GetFullName());

  // Main function.
  Identifier main(IdentifierQualification::kRelative,
                  IdentifierComponent(SpecialIdentifier::kMain));
  EXPECT_EQ("$main", main.GetFullName());
}

TEST(Identifier, GetScope) {
  std::string name1("Name1");
  std::string name2("Name2");
  std::string name3("Name3");

  // "" -> "".
  Identifier empty;
  EXPECT_EQ("", empty.GetScope().GetDebugName());

  // "::" -> "::".
  Identifier scope_only(IdentifierQualification::kGlobal);
  EXPECT_EQ("::", scope_only.GetScope().GetDebugName());

  // "Name1" -> "".
  Identifier name_only(IdentifierQualification::kRelative, IdentifierComponent(name1));
  EXPECT_EQ("", name_only.GetScope().GetDebugName());

  // ::Name1" -> "::".
  Identifier scoped_name(IdentifierQualification::kGlobal, IdentifierComponent(name1));
  EXPECT_EQ("::", scoped_name.GetScope().GetDebugName());

  // "Name1::Name2" -> "Name1".
  Identifier two_names(IdentifierQualification::kRelative, IdentifierComponent(name1));
  two_names.AppendComponent(IdentifierComponent(name2));
  EXPECT_EQ("\"Name1\"", two_names.GetScope().GetDebugName());

  // "::Name1::Name2" -> "::Name1".
  Identifier two_scoped_names(IdentifierQualification::kGlobal, IdentifierComponent(name1));
  two_scoped_names.AppendComponent(IdentifierComponent(name2));
  EXPECT_EQ("::\"Name1\"", two_scoped_names.GetScope().GetDebugName());

  // "Name1::Name2::Name3" -> "Name1::Name2".
  Identifier three_scoped_names(IdentifierQualification::kRelative, IdentifierComponent(name1));
  three_scoped_names.AppendComponent(IdentifierComponent(name2));
  three_scoped_names.AppendComponent(IdentifierComponent(name3));
  EXPECT_EQ("\"Name1\"; ::\"Name2\"", three_scoped_names.GetScope().GetDebugName());
}

}  // namespace zxdb
