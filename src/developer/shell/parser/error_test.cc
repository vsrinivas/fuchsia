// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/error.h"

#include <gtest/gtest.h>

#include "src/developer/shell/parser/ast_test.h"
#include "src/developer/shell/parser/combinators.h"
#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {

TEST(ErrorTest, Insert) {
  const char* kTestString = "smith";

  auto result =
      Seq(Alt(Token("bob"), ErInsert("Expected bob")), Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(5u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(E[Expected bob] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, Skip) {
  const char* kTestString = "bobsmith";

  auto result = Seq(Alt(Token("jed"), ErSkip("Unexpected bob", Token("bob"))),
                    Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(8u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(E[Unexpected bob] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SkipMatchMacro) {
  const char* kTestString = "jedbobsmith";

  auto result = Seq(Alt(Token("fred"), ErSkip("Unexpected '%MATCH%'", OnePlus(AnyCharBut("s")))),
                    Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(11u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(E[Unexpected 'jedbob'] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

}  // namespace shell::parser
