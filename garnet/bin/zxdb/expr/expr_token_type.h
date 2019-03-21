// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
  kTrue,         // true
  kFalse,        // false
  kConst,        // const
  kVolatile,     // volatile
  kRestrict,     // restrict

  // Keep last. Not a token, but the count of tokens.
  kNumTypes
};

}  // namespace zxdb
