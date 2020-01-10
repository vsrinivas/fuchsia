// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/identifier_glob.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

using Score = std::optional<int>;

// Parses the given idetifier and asserts success. Used for constant input in this test.
ParsedIdentifier Parse(const std::string& s) {
  ParsedIdentifier result;

  Err err = ExprParser::ParseIdentifier(s, &result);
  FXL_CHECK(!err.has_error()) << err.msg();

  return result;
}

}  // namespace

TEST(IdentifierGlob, Exact) {
  IdentifierGlob myclass;
  ASSERT_FALSE(myclass.Init("MyClass").has_error());

  // Exact match.
  EXPECT_EQ(Score(0), myclass.Matches(Parse("MyClass")));

  // The identifier parser will trim whitespace.
  EXPECT_EQ(Score(0), myclass.Matches(Parse("MyClass ")));
  EXPECT_EQ(Score(0), myclass.Matches(Parse(" MyClass")));

  // Non-matches.
  EXPECT_EQ(std::nullopt, myclass.Matches(Parse("MyClassA")));
  EXPECT_EQ(std::nullopt, myclass.Matches(Parse("MyClas")));
  EXPECT_EQ(std::nullopt, myclass.Matches(Parse("myclass")));
  EXPECT_EQ(std::nullopt, myclass.Matches(Parse("MyClass<>")));

  // Global qualification in either direction doesn't matter.
  IdentifierGlob global_myclass;
  ASSERT_FALSE(global_myclass.Init("::MyClass").has_error());
  EXPECT_EQ(Score(0), global_myclass.Matches(Parse("::MyClass")));
  EXPECT_EQ(Score(0), global_myclass.Matches(Parse("MyClass")));
}

TEST(IdentifierGlob, EmptyTemplates) {
  IdentifierGlob no_star;
  ASSERT_FALSE(no_star.Init("MyClass<>").has_error());

  // Template existance must match.
  EXPECT_EQ(Score(0), no_star.Matches(Parse("MyClass<>")));  // Technically invalid C++.
  EXPECT_EQ(std::nullopt, no_star.Matches(Parse("MyClass")));
}

TEST(IdentifierGlob, OneTemplate) {
  IdentifierGlob one_star;
  ASSERT_FALSE(one_star.Init("MyClass<*>").has_error());

  // "*" won't match no template params.
  EXPECT_EQ(std::nullopt, one_star.Matches(Parse("MyClass<>")));

  // String globs will match this, but our identifier one won't.
  EXPECT_EQ(std::nullopt, one_star.Matches(Parse("MyClass<int>::Something<int>")));

  // Will eat multiple params.
  EXPECT_EQ(Score(1), one_star.Matches(Parse("MyClass<int>")));
  EXPECT_EQ(Score(2), one_star.Matches(Parse("MyClass<int, int>")));
  EXPECT_EQ(Score(3), one_star.Matches(Parse("MyClass<int,int,double>")));
}

TEST(IdentifierGlob, TwoTemplate) {
  IdentifierGlob one_star;
  ASSERT_FALSE(one_star.Init("MyClass<*,*>").has_error());

  // Won't match fewer template params.
  EXPECT_EQ(std::nullopt, one_star.Matches(Parse("MyClass<>")));
  EXPECT_EQ(std::nullopt, one_star.Matches(Parse("MyClass<int>")));
  EXPECT_EQ(std::nullopt, one_star.Matches(Parse("MyClass<Something<Foo>>")));

  EXPECT_EQ(Score(1), one_star.Matches(Parse("MyClass<int, Something<Foo>>")));
  EXPECT_EQ(Score(2), one_star.Matches(Parse("MyClass<int,int,double>")));
}

TEST(IdentifierGlob, LiteralTemplate) {
  IdentifierGlob one_lit;
  ASSERT_FALSE(one_lit.Init("MyClass<int>").has_error());

  EXPECT_EQ(std::nullopt, one_lit.Matches(Parse("MyClass<>")));
  EXPECT_EQ(std::nullopt, one_lit.Matches(Parse("MyClass<float>")));
  EXPECT_EQ(std::nullopt, one_lit.Matches(Parse("MyClass<int, int>")));
  EXPECT_EQ(std::nullopt, one_lit.Matches(Parse("MyClass<int*>")));

  EXPECT_EQ(Score(0), one_lit.Matches(Parse("MyClass<int>")));

  // * not by itself is a literal (in this case, a pointer).
  IdentifierGlob lit_ptr;
  ASSERT_FALSE(lit_ptr.Init("MyClass<int*>").has_error());
  EXPECT_EQ(std::nullopt, lit_ptr.Matches(Parse("MyClass<int>")));
  EXPECT_EQ(Score(0), lit_ptr.Matches(Parse("MyClass<int* >")));

  IdentifierGlob two_lit;
  ASSERT_FALSE(two_lit.Init("MyClass<int, float>").has_error());

  EXPECT_EQ(std::nullopt, two_lit.Matches(Parse("MyClass<int, int>")));
  EXPECT_EQ(Score(0), two_lit.Matches(Parse("MyClass <int,float >")));
}

TEST(IdentifierGlob, StarLiteralTemplate) {
  IdentifierGlob star_lit;
  ASSERT_FALSE(star_lit.Init("MyClass<*, int>").has_error());

  EXPECT_EQ(std::nullopt, star_lit.Matches(Parse("MyClass<int>")));
  EXPECT_EQ(Score(1), star_lit.Matches(Parse("MyClass<double,int>")));

  // "*" not greedy unless last.
  EXPECT_EQ(std::nullopt, star_lit.Matches(Parse("MyClass<double, double, int>")));

  EXPECT_EQ(std::nullopt, star_lit.Matches(Parse("MyClass<double, int, double>")));
}

TEST(IdentifierGlob, LiteralStarTemplate) {
  IdentifierGlob lit_star;
  ASSERT_FALSE(lit_star.Init("MyClass<int,*>").has_error());

  EXPECT_EQ(std::nullopt, lit_star.Matches(Parse("MyClass<int>")));
  EXPECT_EQ(Score(1), lit_star.Matches(Parse("MyClass<int,int>")));
  EXPECT_EQ(Score(2), lit_star.Matches(Parse("MyClass<int,double,float>")));
}

}  // namespace zxdb
