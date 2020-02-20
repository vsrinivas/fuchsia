// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parser.h"

#include "gtest/gtest.h"
#include "src/developer/shell/parser/ast.h"

namespace shell::parser {

TEST(ParserTest, VariableDecl) {
  const auto kTestString = "var s = 0";

  auto parse = Parse(kTestString);

  EXPECT_EQ("Program(VariableDecl('var' 's' '=' '0'))", parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclFail) {
  const auto kTestString = "vars = 0";

  auto parse = Parse(kTestString);

  EXPECT_EQ("Program(E[Unexpected 'vars = 0'])", parse->ToString(kTestString));
}

TEST(ParserTest, TwoVariableDecl) {
  const auto kTestString = "var x = 0;\nvar y = 0";

  auto parse = Parse(kTestString);

  EXPECT_EQ("Program(VariableDecl('var' 'x' '=' '0') ';' VariableDecl('var' 'y' '=' '0'))",
            parse->ToString(kTestString));
}

TEST(ParserTest, TwoVariableDeclFail) {
  const auto kTestString = "varx = 0;\nvar y = 0";

  auto parse = Parse(kTestString);

  EXPECT_EQ("Program(E[Unexpected 'varx = 0;\nvar y = 0'])", parse->ToString(kTestString));
}

TEST(ParserTest, TwoVariableDeclTrailingChars) {
  const auto kTestString = "var x = 0;\nvar y = 0;\nxxx";

  auto parse = Parse(kTestString);

  EXPECT_EQ(
      "Program(VariableDecl('var' 'x' '=' '0') ';' VariableDecl('var' 'y' '=' '0') ';' "
      "E[Unexpected 'xxx'])",
      parse->ToString(kTestString));
}

}  // namespace shell::parser
