// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/identifier.h"
#include "garnet/bin/zxdb/common/err.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(Identifier, GetFullName) {
  // Empty.
  Identifier unqualified;
  EXPECT_EQ("", unqualified.GetFullName());

  // Single name with no "::" at the beginning.
  unqualified.AppendComponent(ExprToken(),
                              ExprToken(ExprToken::kName, "First", 2));
  EXPECT_EQ("First", unqualified.GetFullName());

  // Single name with a "::" at the beginning.
  Identifier qualified;
  qualified.AppendComponent(ExprToken(ExprToken::kColonColon, "::", 0),
                            ExprToken(ExprToken::kName, "First", 2));
  EXPECT_EQ("::First", qualified.GetFullName());

  // Append some template stuff.
  qualified.AppendComponent(ExprToken(ExprToken::kColonColon, "::", 7),
                            ExprToken(ExprToken::kName, "Second", 9),
                            ExprToken(ExprToken::kLess, "<", 15),
                            {"int", "Foo"},
                            ExprToken(ExprToken::kGreater, ">", 24));
  EXPECT_EQ("::First::Second<int, Foo>", qualified.GetFullName());
}

TEST(Identifier, GetScope) {
  ExprToken colon_colon(ExprToken::kColonColon, "::", 0);
  ExprToken name1(ExprToken::kName, "Name1", 100);
  ExprToken name2(ExprToken::kName, "Name2", 100);
  ExprToken name3(ExprToken::kName, "Name3", 100);

  // "" -> "".
  Identifier empty;
  EXPECT_EQ("", empty.GetScope().GetDebugName());

  // "::" -> "::".
  Identifier scope_only(Identifier::Component(colon_colon, ExprToken()));
  EXPECT_EQ("::,\"\"", scope_only.GetScope().GetDebugName());

  // "Name1" -> "".
  Identifier name_only(Identifier::Component(ExprToken(), name1));
  EXPECT_EQ("", name_only.GetScope().GetDebugName());

  // ::Name1" -> "::".
  Identifier scoped_name(Identifier::Component(colon_colon, name1));
  EXPECT_EQ("::,\"\"", scoped_name.GetScope().GetDebugName());

  // "Name1::Name2" -> "Name1".
  Identifier two_names(Identifier::Component(ExprToken(), name1));
  two_names.AppendComponent(Identifier::Component(colon_colon, name2));
  EXPECT_EQ("\"Name1\"", two_names.GetScope().GetDebugName());

  // "::Name1::Name2" -> "::Name1".
  Identifier two_scoped_names(Identifier::Component(colon_colon, name1));
  two_scoped_names.AppendComponent(Identifier::Component(colon_colon, name2));
  EXPECT_EQ("::,\"Name1\"", two_scoped_names.GetScope().GetDebugName());

  // "Name1::Name2::Name3" -> "Name1::Name2".
  Identifier three_scoped_names(Identifier::Component(ExprToken(), name1));
  three_scoped_names.AppendComponent(Identifier::Component(colon_colon, name2));
  three_scoped_names.AppendComponent(Identifier::Component(colon_colon, name3));
  EXPECT_EQ("\"Name1\"; ::,\"Name2\"", three_scoped_names.GetScope().GetDebugName());
}

TEST(Identifier, FromString) {
  // Empty input.
  auto[empty_err, empty_ident] = Identifier::FromString("");
  EXPECT_TRUE(empty_err.has_error());
  EXPECT_EQ("No input to parse.", empty_err.msg());
  EXPECT_EQ("", empty_ident.GetDebugName());

  // Normal word.
  auto[word_err, word_ident] = Identifier::FromString("foo");
  EXPECT_FALSE(word_err.has_error());
  EXPECT_EQ("\"foo\"", word_ident.GetDebugName());

  // Complicated identifier (copied from STL).
  auto[complex_err, complex_ident] = Identifier::FromString(
      "std::unordered_map<"
      "std::__2::basic_string<char>, "
      "unsigned long, "
      "std::__2::hash<std::__2::basic_string<char> >, "
      "std::__2::equal_to<std::__2::basic_string<char> >, "
      "std::__2::allocator<std::__2::pair<const std::__2::basic_string<char>, "
      "unsigned long> >>");
  EXPECT_FALSE(complex_err.has_error());
  EXPECT_EQ(
      "\"std\"; "
      "::,"
      "\"unordered_map\","
      "<\"std::__2::basic_string<char>\", "
      "\"unsigned long\", "
      "\"std::__2::hash<std::__2::basic_string<char>>\", "
      "\"std::__2::equal_to<std::__2::basic_string<char>>\", "
      "\"std::__2::allocator<std::__2::pair<"
      "const std::__2::basic_string<char>, unsigned long>>\">",
      complex_ident.GetDebugName());

  // Error from input.
  auto[bad_err, bad_ident] = Identifier::FromString("Foo<Bar");
  EXPECT_TRUE(bad_err.has_error());
  EXPECT_EQ("Expected '>' to match. Hit the end of input instead.",
            bad_err.msg());
  EXPECT_EQ("", bad_ident.GetDebugName());
}

}  // namespace zxdb
