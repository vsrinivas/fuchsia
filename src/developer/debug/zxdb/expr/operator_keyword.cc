// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/operator_keyword.h"

#include <ctype.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <utility>

namespace zxdb {

namespace {

constexpr size_t kMaxOps = 3;  // Can have at most 3 operators in a row ("new[]").

// This list is searched in-order so more specific tokens (longer ones) need to go first.
const ExprTokenType kOverloadableOperators[][kMaxOps] = {
    // clang-format off

  // Operators with triple tokens,
  {ExprTokenType::kNew,    ExprTokenType::kLeftSquare, ExprTokenType::kRightSquare},  // new[]
  {ExprTokenType::kDelete, ExprTokenType::kLeftSquare, ExprTokenType::kRightSquare},  // delete[]

  // Operators with double tokens. Note that the tokenizer generates two tokens for ">>" because of
  // C++'s ambiguity so we need to treat that as a double one.
  {ExprTokenType::kLeftParen,  ExprTokenType::kRightParen,   ExprTokenType::kInvalid},  // operator()
  {ExprTokenType::kLeftSquare, ExprTokenType::kRightSquare,  ExprTokenType::kInvalid},  // operator[]
  {ExprTokenType::kGreater,    ExprTokenType::kGreater,      ExprTokenType::kInvalid},  // operator>>
  {ExprTokenType::kGreater,    ExprTokenType::kGreaterEqual, ExprTokenType::kInvalid},  // operator>>=

  // Operators with single tokens.
  {ExprTokenType::kPlus,             ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kMinus,            ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kStar,             ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kSlash,            ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kPercent,          ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kCaret,            ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kAmpersand,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kBitwiseOr,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kTilde,            ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kBang,             ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kEquals,           ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kLess,             ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kGreater,          ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kPlusEquals,       ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kMinusEquals,      ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kStarEquals,       ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kSlashEquals,      ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kPercentEquals,    ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kCaretEquals,      ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kAndEquals,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kOrEquals,         ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kShiftLeft,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kShiftRight,       ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kShiftLeftEquals,  ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kShiftRightEquals, ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kEquality,         ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kInequality,       ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kLessEqual,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kGreaterEqual,     ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kDoubleAnd,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kLogicalOr,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kPlusPlus,         ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kMinusMinus,       ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kComma,            ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kArrowStar,        ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kArrow,            ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kNew,              ExprTokenType::kInvalid, ExprTokenType::kInvalid},
  {ExprTokenType::kDelete,           ExprTokenType::kInvalid, ExprTokenType::kInvalid},

    // clang-format on
};

// Makes a name like "operator<" or "operator[]" given a squence of operator token types.
std::string MakeCanonicalOperatorName(const ExprTokenType types[], size_t count) {
  std::string result = "operator";
  for (size_t i = 0; i < count; i++) {
    const std::string_view& op_str = RecordForTokenType(types[i]).static_value;
    if (isalnum(op_str[0]))
      result.push_back(' ');  // Alphanumeric operators like "new" and "delete" need a space.
    result.append(op_str);
  }
  return result;
}

// Validates that the given sequences of tokens matches the given sequence of types. Both input
// arrays must be at least |count| long.
bool TokenSequenceMatches(const ExprToken tokens[], const ExprTokenType match[], size_t count) {
  for (size_t i = 0; i < count; i++) {
    // All token values must be equal.
    if (tokens[i].type() != match[i])
      return false;

    // The tokens must be sequential with no whitespace between them.
    if (i > 0 && !tokens[i - 1].ImmediatelyPrecedes(tokens[i]))
      return false;
  }
  return true;
}

}  // namespace

OperatorKeywordResult ParseOperatorKeyword(const std::vector<ExprToken>& tokens,
                                           size_t keyword_token) {
  // The keyword token should always be "operator".
  FX_DCHECK(tokens[keyword_token].type() == ExprTokenType::kOperator);

  // Which operator will follow the keyword.
  size_t tokens_begin = keyword_token + 1;
  if (tokens_begin >= tokens.size())
    return OperatorKeywordResult();  // Nothing following "operator".

  // This is just brute-force. It could be optimized by sorting if needed but parsing operators is
  // not performance-critical.
  for (const auto& candidate : kOverloadableOperators) {
    // See how many tokens this sequence needs to match (the number of non-invalid token types).
    // This could be done in one loop combined with the below, but the logic becomes more difficult
    // to follow.
    size_t match_count = 0;
    for (size_t i = 0; i < kMaxOps && candidate[i] != ExprTokenType::kInvalid; i++)
      match_count++;

    if (tokens_begin + match_count > tokens.size())
      continue;  // Not enough room to match this one.

    if (TokenSequenceMatches(&tokens[tokens_begin], candidate, match_count)) {
      return OperatorKeywordResult{
          .success = true,
          .canonical_name = MakeCanonicalOperatorName(candidate, match_count),
          .end_token = tokens_begin + match_count,
      };
    }
  }
  return OperatorKeywordResult();
}

}  // namespace zxdb
