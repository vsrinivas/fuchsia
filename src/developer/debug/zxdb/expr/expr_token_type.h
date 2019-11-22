// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_TYPE_H_

#include <string_view>

namespace zxdb {

// This type must start at 0 and increment monotonically since it is used
// as an index into the parser lookup table.
enum class ExprTokenType : size_t {
  kInvalid = 0,
  kName,          // random_text
  kInteger,       // 123, 0x89ab
  kEquals,        // =
  kEquality,      // ==
  kInequality,    // !=
  kLessEqual,     // <=
  kGreaterEqual,  // >=
  kSpaceship,     // <=>
  kDot,           // .
  kComma,         // ,
  kSemicolon,     // ;
  kStar,          // *
  kAmpersand,     // &
  kDoubleAnd,     // && (logical "and" or rvalue reference)
  kBitwiseOr,     // |
  kLogicalOr,     // ||
  kArrow,         // ->
  kLeftSquare,    // [
  kRightSquare,   // ]
  kLeftParen,     // (
  kRightParen,    // )
  kLess,          // <
  kGreater,       // >
  kMinus,         // - (by itself, not part of "->")
  kBang,          // !
  kPlus,          // +
  kSlash,         // /
  kCaret,         // ^
  kPercent,       // %
  kColonColon,    // ::
  kShiftLeft,     // <<

  // The shift right token is not produced by the tokenizer which will always produce two adjacent
  // ">" tokens. The parser will disambiguate ">>" as a shift operator vs. two template endings and
  // generate a "shift right" at that time.
  kShiftRight,  // >>

  // Special keywords.
  kTrue,             // true
  kFalse,            // false
  kConst,            // const
  kMut,              // mut
  kVolatile,         // volatile
  kRestrict,         // restrict
  kReinterpretCast,  // reinterpret_cast
  kStaticCast,       // static_cast
  kSizeof,           // sizeof
  kAs,               // as

  // Keep last. Not a token, but the count of tokens.
  kNumTypes
};

constexpr size_t kNumExprTokenTypes = static_cast<size_t>(ExprTokenType::kNumTypes);

struct ExprTokenRecord {
  constexpr ExprTokenRecord() = default;
  constexpr ExprTokenRecord(ExprTokenType t, unsigned langs,
                            std::string_view static_val = std::string_view());

  ExprTokenType type = ExprTokenType::kInvalid;

  // Nonempty when this token type contains a known string, e.g. "&&" rather
  // than some arbitrary name.
  std::string_view static_value;

  // Set to true when the static value of this token is alphanumeric such that
  // to separate it from another token requires a non-alphanumeric character.
  bool is_alphanum = false;

  // A bitfield consisting of a combination of ExprLanguage values.
  unsigned languages = 0;
};

const ExprTokenRecord& RecordForTokenType(ExprTokenType);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_TYPE_H_
