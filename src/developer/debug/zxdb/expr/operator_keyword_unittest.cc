// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/operator_keyword.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"

namespace zxdb {

namespace {

void CheckParseOperator(const std::string& input, OperatorKeywordResult expected,
                        size_t start_token = 0) {
  ExprTokenizer tokenizer(input);
  EXPECT_TRUE(tokenizer.Tokenize());

  OperatorKeywordResult result = ParseOperatorKeyword(tokenizer.tokens(), start_token);
  EXPECT_EQ(expected.success, result.success) << "For " << input;
  EXPECT_EQ(expected.canonical_name, result.canonical_name) << "For " << input;
  EXPECT_EQ(expected.end_token, result.end_token) << "For " << input;
}

}  // namespace

TEST(OperatorKeyword, Parse) {
  // Nothing following the keyword.
  CheckParseOperator("operator", OperatorKeywordResult());

  // Invalid thing following the keyword.
  CheckParseOperator("operator hello", OperatorKeywordResult());
  CheckParseOperator("operator.", OperatorKeywordResult());

  CheckParseOperator(
      "operator+",
      OperatorKeywordResult{.success = true, .canonical_name = "operator+", .end_token = 2});
  CheckParseOperator(
      "operator,",
      OperatorKeywordResult{.success = true, .canonical_name = "operator,", .end_token = 2});
  CheckParseOperator(
      "operator  +",
      OperatorKeywordResult{.success = true, .canonical_name = "operator+", .end_token = 2});
  CheckParseOperator(
      "operator >>",
      OperatorKeywordResult{.success = true, .canonical_name = "operator>>", .end_token = 3});
  CheckParseOperator(
      "operator > >",
      OperatorKeywordResult{.success = true, .canonical_name = "operator>", .end_token = 2});
  CheckParseOperator(
      "operator<<",
      OperatorKeywordResult{.success = true, .canonical_name = "operator<<", .end_token = 2});

  // Valid stuff following the keyword.
  CheckParseOperator(
      "operator +;",
      OperatorKeywordResult{.success = true, .canonical_name = "operator+", .end_token = 2});
  CheckParseOperator(
      "operator/(hello)",
      OperatorKeywordResult{.success = true, .canonical_name = "operator/", .end_token = 2});

  // Stuff before and after.
  CheckParseOperator(
      "void operator()()",
      OperatorKeywordResult{.success = true, .canonical_name = "operator()", .end_token = 4}, 1);
}

}  // namespace zxdb
