// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/template_type_extractor.h"
#include "garnet/bin/zxdb/expr/expr_token.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(TemplateTypeExtractor, Basic) {
  // No template contents: "Foo<>". When extracing the type, we'll be given
  // the first token after the template opening (the 2nd token, ">").
  TemplateTypeResult result =
      ExtractTemplateType({ExprToken(ExprToken::kName, "Foo", 0),
                           ExprToken(ExprToken::kLess, "<", 3),
                           ExprToken(ExprToken::kGreater, ">", 4)},
                          2);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(2u, result.end_token);
  EXPECT_EQ("", result.canonical_name);

  // Unterminated template argument list: "Foo<<".
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "Foo", 0),
                                ExprToken(ExprToken::kLess, "<", 3),
                                ExprToken(ExprToken::kLess, "<", 4)},
                               2);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(2u, result.unmatched_error_token);
  EXPECT_EQ(3u, result.end_token);

  // What would appear in "std::vector<int>":
  // "int>" -> "int"
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "int", 1),
                                ExprToken(ExprToken::kGreater, ">", 4)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(1u, result.end_token);
  EXPECT_EQ("int", result.canonical_name);

  // What would appear in "std::vector<const int*>":
  // "const int*>" -> "const int*"
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "const", 1),
                                ExprToken(ExprToken::kName, "int", 1),
                                ExprToken(ExprToken::kStar, "*", 1),
                                ExprToken(ExprToken::kGreater, ">", 4)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(3u, result.end_token);
  EXPECT_EQ("const int*", result.canonical_name);

  // What would appear in "(const Foo&)foo"
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "const", 0),
                                ExprToken(ExprToken::kName, "Foo", 7),
                                ExprToken(ExprToken::kAmpersand, "&", 10),
                                ExprToken(ExprToken::kRightParen, ")", 11)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(3u, result.end_token);
  EXPECT_EQ("const Foo&", result.canonical_name);

  // What would appear in "std::map<int, int>":
  // "int," -> "int"
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "int", 1),
                                ExprToken(ExprToken::kGreater, ",", 4)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(1u, result.end_token);
  EXPECT_EQ("int", result.canonical_name);

  // What would appear in
  // "std::allocator<int, 6>>" -> "std::allocator<int, 6>"
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "std", 1),
                                ExprToken(ExprToken::kColonColon, "::", 4),
                                ExprToken(ExprToken::kName, "allocator", 6),
                                ExprToken(ExprToken::kLess, "<", 15),
                                ExprToken(ExprToken::kName, "int", 16),
                                ExprToken(ExprToken::kComma, ",", 19),
                                ExprToken(ExprToken::kInteger, "6", 21),
                                ExprToken(ExprToken::kGreater, ">", 22),
                                ExprToken(ExprToken::kGreater, ">", 23)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(8u, result.end_token);
  EXPECT_EQ("std::allocator<int, 6>", result.canonical_name);
}

TEST(TemplateTypeExtractor, WeirdCommas) {
  // As in "Foo<operator,, 2>" -> "operator,"
  TemplateTypeResult result =
      ExtractTemplateType({ExprToken(ExprToken::kName, "operator", 0),
                           ExprToken(ExprToken::kComma, ",", 8),
                           ExprToken(ExprToken::kComma, ",", 9)},
                          0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(2u, result.end_token);
  EXPECT_EQ("operator,", result.canonical_name);

  // As in "Foo<Bar<operator,, 2>>"
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "Bar", 0),
                                ExprToken(ExprToken::kLess, "<", 4),
                                ExprToken(ExprToken::kName, "operator", 5),
                                ExprToken(ExprToken::kComma, ",", 13),
                                ExprToken(ExprToken::kComma, ",", 14),
                                ExprToken(ExprToken::kInteger, "2", 15),
                                ExprToken(ExprToken::kGreater, ">", 16),
                                ExprToken(ExprToken::kGreater, ">", 17)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(7u, result.end_token);
  EXPECT_EQ("Bar<operator,, 2>", result.canonical_name);
}

TEST(TemplateTypeExtractor, WeirdAngleBrackets) {
  // As in "std::map<int, int, operator<>".
  TemplateTypeResult result =
      ExtractTemplateType({ExprToken(ExprToken::kName, "operator", 0),
                           ExprToken(ExprToken::kLess, "<", 8),
                           ExprToken(ExprToken::kGreater, ">", 9)},
                          0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(2u, result.end_token);
  EXPECT_EQ("operator<", result.canonical_name);

  // As in "std::map<int, int, operator> >". The > are non-adjacent so don't
  // get treated as a single operator.
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "operator", 0),
                                ExprToken(ExprToken::kGreater, ">", 8),
                                ExprToken(ExprToken::kGreater, ">", 10)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(2u, result.end_token);
  EXPECT_EQ("operator>", result.canonical_name);

  // As in "std::map<int, int, operator>>>". This is passing "operator>>" to a
  // template.
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "operator", 0),
                                ExprToken(ExprToken::kGreater, ">", 8),
                                ExprToken(ExprToken::kGreater, ">", 9),
                                ExprToken(ExprToken::kGreater, ">", 10)},
                               0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(3u, result.end_token);
  EXPECT_EQ("operator>>", result.canonical_name);
}

TEST(TemplateTypeExtractor, OtherOperator) {
  // As in "Foo<operator ++>
  TemplateTypeResult result =
      ExtractTemplateType({ExprToken(ExprToken::kName, "operator", 0),
                           ExprToken(ExprToken::kPlus, "+", 9),
                           ExprToken(ExprToken::kPlus, "+", 10),
                           ExprToken(ExprToken::kGreater, ">", 11)},
                          0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(3u, result.end_token);
  EXPECT_EQ("operator++", result.canonical_name);

  // Malformed input with "operator" at end. Just returns the same thing since
  // we're not trying to validate proper C++, only validate that we found the
  // extent of the declaration.
  result = ExtractTemplateType({ExprToken(ExprToken::kName, "operator", 0)}, 0);
  EXPECT_TRUE(result.success);
  EXPECT_EQ(1u, result.end_token);
  EXPECT_EQ("operator", result.canonical_name);
}

}  // namespace zxdb
