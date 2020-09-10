// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/developer/shell/parser/ast_test.h"
#include "src/developer/shell/parser/combinators.h"
#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {

TEST(CombinatorTest, Seq) {
  const char* kTestString = "bobsmith";

  auto result = Seq(Token("bob"), Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(8u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'smith')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, Seq3) {
  const char* kTestString = "bobsmithjones";

  auto result = Seq(Token("bob"), Token("smith"), Token("jones"))(ParseResult(kTestString));

  ASSERT_TRUE(result);
  EXPECT_EQ(13u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'smith' 'jones')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, AltA) {
  const char* kTestString = "bob";

  auto result = Alt(Token("bob"), Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, AltB) {
  const char* kTestString = "smith";

  auto result = Alt(Token("bob"), Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(5u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('smith')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, Not) {
  const char* kTestString = "smith";

  auto result = Not(Token("bob"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("()", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, NotFail) {
  const char* kTestString = "bob";

  auto result = Not(Token("bob"))(ParseResult(kTestString));

  EXPECT_FALSE(result);
}

TEST(CombinatorTest, Maybe) {
  const char* kTestString = "bob";

  auto result = Maybe(Token("bob"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, MaybeFail) {
  const char* kTestString = "smith";

  auto result = Maybe(Token("bob"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("()", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, Multi) {
  const char* kTestString = "bobbobbobbob";

  auto result = Multi(3, 5, Token("bob"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(12u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'bob' 'bob' 'bob')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, MultiOverflow) {
  const char* kTestString = "bobbobbobbobbobbobbob";  // 7 bobs.

  auto result = Multi(3, 5, Token("bob"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(15u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'bob' 'bob' 'bob' 'bob')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, MultiFail) {
  const char* kTestString = "bobbob";

  auto result = Multi(3, 5, Token("bob"))(ParseResult(kTestString));
  ASSERT_FALSE(result);
}

TEST(CombinatorTest, OnePlus) {
  const char* kTestString = "bobbobbobbob";

  auto result = OnePlus(Token("bob"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(12u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'bob' 'bob' 'bob')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, OnePlusFail) {
  const char* kTestString = "";

  auto result = OnePlus(Token("bob"))(ParseResult(kTestString));
  ASSERT_FALSE(result);
}

}  // namespace shell::parser
