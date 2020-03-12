// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/parsed_identifier.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"

namespace zxdb {

TEST(ParsedIdentifier, GetName) {
  // Empty.
  ParsedIdentifier unqualified;
  EXPECT_EQ("", unqualified.GetFullName());

  // Single name with no "::" at the beginning.
  unqualified.AppendComponent(ParsedIdentifierComponent("First"));
  EXPECT_EQ("First", unqualified.GetFullName());

  // Single name with a "::" at the beginning.
  ParsedIdentifier qualified(IdentifierQualification::kGlobal, ParsedIdentifierComponent("First"));
  EXPECT_EQ("::First", qualified.GetFullName());

  // Append some template stuff.
  qualified.AppendComponent(ParsedIdentifierComponent("Second", {"int", "Foo"}));
  EXPECT_EQ("::First::Second<int, Foo>", qualified.GetFullName());
  EXPECT_EQ("::\"First\"; ::\"Second\",<\"int\", \"Foo\">", qualified.GetDebugName());

  // PLT function.
  ParsedIdentifier plt(IdentifierQualification::kRelative,
                       ParsedIdentifierComponent(SpecialIdentifier::kPlt, "zx_foo_bar"));
  EXPECT_EQ("$plt(zx_foo_bar)", plt.GetFullName());

  // Main function.
  ParsedIdentifier main(IdentifierQualification::kRelative,
                        ParsedIdentifierComponent(SpecialIdentifier::kMain));
  EXPECT_EQ("$main", main.GetFullName());
}

TEST(ParsedIdentifier, ToIdentifier) {
  ParsedIdentifier empty;
  Identifier empty_out = ToIdentifier(empty);
  EXPECT_EQ(Identifier(), empty_out);
  EXPECT_EQ(0u, empty_out.components().size());

  ParsedIdentifier complex;
  Err err = ExprParser::ParseIdentifier("::std::vector<int>::iterator", &complex);
  ASSERT_FALSE(err.has_error());

  Identifier complex_out = ToIdentifier(complex);
  EXPECT_EQ("::\"std\"; ::\"vector<int>\"; ::\"iterator\"", complex_out.GetDebugName());

  // PLT function.
  ParsedIdentifier plt(IdentifierQualification::kRelative,
                       ParsedIdentifierComponent(SpecialIdentifier::kPlt, "zx_foo_bar"));
  EXPECT_EQ("\"$plt(zx_foo_bar)\"", ToIdentifier(plt).GetDebugName());

  // Main function.
  ParsedIdentifier main(IdentifierQualification::kRelative,
                        ParsedIdentifierComponent(SpecialIdentifier::kMain));
  EXPECT_EQ("\"$main\"", ToIdentifier(main).GetDebugName());
}

TEST(ParsedIdentifier, ToParsedIdentifier) {
  Identifier empty;
  ParsedIdentifier empty_out = ToParsedIdentifier(empty);
  EXPECT_EQ(ParsedIdentifier(), empty_out);
  EXPECT_EQ(0u, empty_out.components().size());

  // Round-trip this template.
  ParsedIdentifier complex_parsed;
  Err err = ExprParser::ParseIdentifier("::std::vector<int>::iterator", &complex_parsed);
  ASSERT_FALSE(err.has_error());

  Identifier complex_ident = ToIdentifier(complex_parsed);
  ParsedIdentifier complex_parsed2 = ToParsedIdentifier(complex_ident);

  EXPECT_EQ(complex_parsed, complex_parsed2);
  EXPECT_EQ("::\"std\"; ::\"vector\",<\"int\">; ::\"iterator\"", complex_parsed2.GetDebugName());

  // Round-trip an invalid C++ identifier. The "::" in one component should not be split, and the
  // crazy characters should be preserved.
  Identifier ident;
  ident.AppendComponent(IdentifierComponent("vector<int>"));
  ident.AppendComponent(IdentifierComponent("foo::bar"));
  ident.AppendComponent(IdentifierComponent("hello{<"));

  ParsedIdentifier ident_parsed = ToParsedIdentifier(ident);
  EXPECT_EQ("\"vector\",<\"int\">; ::\"foo::bar\"; ::\"hello{<\"", ident_parsed.GetDebugName());

  Identifier ident2 = ToIdentifier(ident_parsed);
  EXPECT_EQ(ident, ident2);
  EXPECT_EQ("\"vector<int>\"; ::\"foo::bar\"; ::\"hello{<\"", ident2.GetDebugName());

  // Check some special names.
  Identifier plt(IdentifierQualification::kRelative,
                 IdentifierComponent(SpecialIdentifier::kPlt, "zx_foo_bar"));
  EXPECT_EQ("\"$plt(zx_foo_bar)\"", ToParsedIdentifier(plt).GetDebugName());

  Identifier main(IdentifierQualification::kRelative,
                  IdentifierComponent(SpecialIdentifier::kMain));
  EXPECT_EQ("\"$main\"", ToParsedIdentifier(main).GetDebugName());
}

}  // namespace zxdb
