// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/identifier.h"
#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

TEST(Identifier, GetName) {
  // Empty.
  Identifier unqualified;
  EXPECT_EQ("", unqualified.GetFullName());
  EXPECT_TRUE(unqualified.GetAsIndexComponents().empty());

  // Single name with no "::" at the beginning.
  unqualified.AppendComponent(ExprToken(),
                              ExprToken(ExprTokenType::kName, "First", 2));
  EXPECT_EQ("First", unqualified.GetFullName());
  std::vector<std::string> expected_index = { "First" };
  EXPECT_EQ(expected_index, unqualified.GetAsIndexComponents());

  // Single name with a "::" at the beginning.
  Identifier qualified;
  qualified.AppendComponent(ExprToken(ExprTokenType::kColonColon, "::", 0),
                            ExprToken(ExprTokenType::kName, "First", 2));
  EXPECT_EQ("::First", qualified.GetFullName());
  EXPECT_EQ(expected_index, qualified.GetAsIndexComponents());

  // Append some template stuff.
  qualified.AppendComponent(ExprToken(ExprTokenType::kColonColon, "::", 7),
                            ExprToken(ExprTokenType::kName, "Second", 9),
                            ExprToken(ExprTokenType::kLess, "<", 15),
                            {"int", "Foo"},
                            ExprToken(ExprTokenType::kGreater, ">", 24));
  EXPECT_EQ("::First::Second<int, Foo>", qualified.GetFullName());
  expected_index.push_back("Second<int, Foo>");
  EXPECT_EQ(expected_index, qualified.GetAsIndexComponents());
}

TEST(Identifier, GetScope) {
  ExprToken colon_colon(ExprTokenType::kColonColon, "::", 0);
  ExprToken name1(ExprTokenType::kName, "Name1", 100);
  ExprToken name2(ExprTokenType::kName, "Name2", 100);
  ExprToken name3(ExprTokenType::kName, "Name3", 100);

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
  EXPECT_EQ("\"Name1\"; ::,\"Name2\"",
            three_scoped_names.GetScope().GetDebugName());
}

TEST(Identifier, InGlobalNamespace) {
  Identifier empty;
  EXPECT_FALSE(empty.InGlobalNamespace());

  Identifier non_global;
  non_global.AppendComponent(Identifier::Component(
      ExprToken(), ExprToken(ExprTokenType::kName, "Foo", 0)));
  EXPECT_FALSE(non_global.InGlobalNamespace());

  Identifier global;
  global.AppendComponent(
      Identifier::Component(ExprToken(ExprTokenType::kColonColon, "::", 0),
                            ExprToken(ExprTokenType::kName, "Foo", 0)));
  EXPECT_TRUE(global.InGlobalNamespace());
}

TEST(Identifier, FromString) {
  // Empty input.
  auto [empty_err, empty_ident] = Identifier::FromString("");
  EXPECT_TRUE(empty_err.has_error());
  EXPECT_EQ("No input to parse.", empty_err.msg());
  EXPECT_EQ("", empty_ident.GetDebugName());

  // Normal word.
  auto [word_err, word_ident] = Identifier::FromString("foo");
  EXPECT_FALSE(word_err.has_error()) << word_err.msg();
  EXPECT_EQ("\"foo\"", word_ident.GetDebugName());

  // Complicated identifier (copied from STL).
  auto [complex_err, complex_ident] = Identifier::FromString(
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
}

TEST(Identifier, FromStringError) {
  // Error from input.
  auto [bad_err, bad_ident] = Identifier::FromString("Foo<Bar");
  EXPECT_TRUE(bad_err.has_error());
  EXPECT_EQ("Expected '>' to match. Hit the end of input instead.",
            bad_err.msg());
  EXPECT_EQ("", bad_ident.GetDebugName());
}

// "PLT" breakpoints are breakpoints set on ELF imports rather than DWARF
// symbols. We need to be able to parse them as an identifier even though it's
// not a valid C++ name. This can be changed in the future if we have a better
// way of identifying these.
TEST(Identifier, PltName) {
  auto [err, ident] = Identifier::FromString("__stack_chk_fail@plt");
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ("\"__stack_chk_fail@plt\"", ident.GetDebugName());
}

}  // namespace zxdb
