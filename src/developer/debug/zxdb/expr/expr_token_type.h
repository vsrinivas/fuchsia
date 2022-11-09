// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_TYPE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_TYPE_H_

#include <string_view>

namespace zxdb {

// This type must start at 0 and increment monotonically since it is used as an index into the
// parser lookup table.
enum class ExprTokenType : size_t {
  kInvalid = 0,
  kName,             // random_text
  kSpecialName,      // $something(perhaps_something_else) for debugger escape sequences.
  kComment,          // "// ..." or "/* ... */" (token value will include the //, /*, */).
  kInteger,          // 123, 0x89ab
  kFloat,            // 0.23e12  1.  2.3f  (never including a leading sign).
  kStringLiteral,    // "foo" (token value will be the 8-bit decoded contents between the quotes).
  kCharLiteral,      // 'a' (8-bit char literal, decoded char will be in Token::value_[0]).
  kRustLifetime,     // 'foobar
  kCommentBlockEnd,  // */ (emitted only when no opening comment token is found)
  kEquals,           // =
  kEquality,         // ==
  kInequality,       // !=
  kLessEqual,        // <=
  kGreaterEqual,     // >=
  kSpaceship,        // <=>
  kDot,              // .
  kDotStar,          // .*
  kComma,            // ,
  kSemicolon,        // ;
  kStar,             // *
  kAmpersand,        // &
  kDoubleAnd,        // && (logical "and" or rvalue reference)
  kBitwiseOr,        // |
  kLogicalOr,        // ||
  kArrow,            // ->
  kArrowStar,        // ->*
  kLeftSquare,       // [
  kRightSquare,      // ]
  kLeftParen,        // (
  kRightParen,       // )
  kLeftBracket,      // {
  kRightBracket,     // }
  kLess,             // <
  kGreater,          // >
  kMinus,            // - (by itself, not part of "->")
  kMinusMinus,       // --
  kBang,             // !
  kPlus,             // +
  kPlusPlus,         // ++
  kSlash,            // /
  kAt,               // @
  kOctothorpe,       // # Treated as a regular operator by the tokenizer for highlighting
                     //   (there is no C preprocessor or Rust annotation support).
  kCaret,            // ^
  kPercent,          // %
  kQuestion,         // ?
  kTilde,            // ~
  kColon,            // :
  kColonColon,       // ::
  kPlusEquals,       // +=
  kMinusEquals,      // -=
  kStarEquals,       // *=
  kSlashEquals,      // /=
  kPercentEquals,    // %=
  kCaretEquals,      // ^=
  kAndEquals,        // &=
  kOrEquals,         // |=
  kShiftLeft,        // <<
  kShiftLeftEquals,  // <<=

  // The shift right token is not produced by the tokenizer which will always produce two adjacent
  // ">" tokens. The parser will disambiguate ">>" as a shift operator vs. two template endings and
  // generate a "shift right" at that time.
  kShiftRight,        // >>
  kShiftRightEquals,  // >>=

  // Special keywords.
  kTrue,             // true
  kFalse,            // false
  kConst,            // const
  kMut,              // mut
  kLet,              // let
  kVolatile,         // volatile
  kRestrict,         // restrict
  kReinterpretCast,  // reinterpret_cast
  kStaticCast,       // static_cast
  kSizeof,           // sizeof
  kAs,               // as
  kIf,               // if
  kElse,             // else
  kFor,              // for
  kDo,               // do
  kWhile,            // while
  kLoop,             // loop (Rust)
  kBreak,            // break
  kOperator,         // operator
  kNew,              // new
  kDelete,           // delete

  // Keep last. Not a token, but the count of tokens.
  kNumTypes
};

constexpr size_t kNumExprTokenTypes = static_cast<size_t>(ExprTokenType::kNumTypes);

struct ExprTokenRecord {
  constexpr ExprTokenRecord() = default;
  constexpr ExprTokenRecord(ExprTokenType t, unsigned langs,
                            std::string_view static_val = std::string_view());

  ExprTokenType type = ExprTokenType::kInvalid;

  // Nonempty when this token type contains a known string, e.g. "&&" rather than some arbitrary
  // name.
  std::string_view static_value;

  // Set to true when the static value of this token is alphanumeric such that to separate it from
  // another token requires a non-alphanumeric character.
  bool is_alphanum = false;

  // A bitfield consisting of a combination of ExprLanguage values.
  unsigned languages = 0;
};

const ExprTokenRecord& RecordForTokenType(ExprTokenType);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_TYPE_H_
