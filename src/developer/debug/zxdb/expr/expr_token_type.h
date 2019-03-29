// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string_view>

namespace zxdb {

// This type must start at 0 and increment monotonically since it is used
// as an index into the parser lookup table.
enum class ExprTokenType : size_t {
  kInvalid = 0,
  kName,         // random_text
  kInteger,      // 123, 0x89ab
  kEquals,       // =
  kEquality,     // ==
  kDot,          // .
  kComma,        // ,
  kStar,         // *
  kAmpersand,    // &
  kDoubleAnd,    // && (logical "and" or rvalue reference)
  kBitwiseOr,    // |
  kLogicalOr,    // ||
  kArrow,        // ->
  kLeftSquare,   // [
  kRightSquare,  // ]
  kLeftParen,    // (
  kRightParen,   // )
  kLess,         // <
  kGreater,      // >
  kMinus,        // - (by itself, not part of "->")
  kPlus,         // +
  kColonColon,   // ::

  // Special keywords.
  kTrue,             // true
  kFalse,            // false
  kConst,            // const
  kVolatile,         // volatile
  kRestrict,         // restrict
  kReinterpretCast,  // reinterpret_cast

  // Keep last. Not a token, but the count of tokens.
  kNumTypes
};

constexpr size_t kNumExprTokenTypes =
    static_cast<size_t>(ExprTokenType::kNumTypes);

struct ExprTokenRecord {
  constexpr ExprTokenRecord() = default;
  constexpr ExprTokenRecord(ExprTokenType t,
                            std::string_view static_val = std::string_view());

  ExprTokenType type = ExprTokenType::kInvalid;

  // Nonempty when this token type contains a known string, e.g. "&&" rather
  // than some arbitrary name.
  std::string_view static_value;

  // Set to true when the static value of this token is alphanumeric such that
  // to separate it from another token requires a non-alphanumeric character.
  bool is_alphanum = false;

  // We will likely need more stuff here such as what languages this token
  // applies to (C, Rust, etc.).
};

const ExprTokenRecord& RecordForTokenType(ExprTokenType);

}  // namespace zxdb
