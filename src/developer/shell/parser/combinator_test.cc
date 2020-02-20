// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/shell/parser/ast_test.h"
#include "src/developer/shell/parser/combinators.h"
#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {

TEST(CombinatorTest, Seq) {
  const char* kTestString = "bobsmith";

  auto match = Seq(Token("bob"), Token("smith"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(8u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'smith')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, Seq3) {
  const char* kTestString = "bobsmithjones";

  auto match = Seq(Token("bob"), Token("smith"), Token("jones"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(13u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'smith' 'jones')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, AltA) {
  const char* kTestString = "bob";

  auto match = Alt(Token("bob"), Token("smith"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, AltB) {
  const char* kTestString = "smith";

  auto match = Alt(Token("bob"), Token("smith"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(5u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('smith')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, Not) {
  const char* kTestString = "smith";

  auto match = Not(Token("bob"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("()", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, NotFail) {
  const char* kTestString = "bob";

  auto match = Not(Token("bob"))(kTestString);

  EXPECT_FALSE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(3u, result.error_score());
  EXPECT_EQ("(E[Ambiguous sequence: 'bob'])",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, Maybe) {
  const char* kTestString = "bob";

  auto match = Maybe(Token("bob"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, MaybeFail) {
  const char* kTestString = "smith";

  auto match = Maybe(Token("bob"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("()", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(CombinatorTest, Multi) {
  const char* kTestString = "bobbobbobbob";

  auto match = Multi(3, 5, Token("bob"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(12u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'bob' 'bob' 'bob')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, MultiOverflow) {
  const char* kTestString = "bobbobbobbobbobbobbob";  // 7 bobs.

  auto match = Multi(3, 5, Token("bob"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(15u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'bob' 'bob' 'bob' 'bob')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, MultiFail) {
  const char* kTestString = "bobbob";

  auto match = Multi(3, 5, Token("bob"))(kTestString);

  EXPECT_FALSE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(6u, result.offset());
  EXPECT_EQ(3u, result.error_score());
  EXPECT_EQ("('bob' 'bob' E[Expected 'bob'])",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, OnePlus) {
  const char* kTestString = "bobbobbobbob";

  auto match = OnePlus(Token("bob"))(kTestString);

  EXPECT_TRUE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(12u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob' 'bob' 'bob' 'bob')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(CombinatorTest, OnePlusFail) {
  const char* kTestString = "";

  auto match = OnePlus(Token("bob"))(kTestString);

  EXPECT_FALSE(match.ok());

  auto result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(3u, result.error_score());
  EXPECT_EQ("(E[Expected 'bob'])", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

}  // namespace shell::parser
