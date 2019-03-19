// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

namespace zxdb {

// A parsed token. This token does not own the strings, it's intended to be
// used only as an intermediate representation pointing into the string being
// parsed.
class ExprToken {
 public:
  // This type must start at 0 and increment monotonically since it is used
  // as an index into the parser lookup table.
  enum Type : int {
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

  ExprToken() = default;
  ExprToken(Type type, const std::string& value, size_t byte_offset)
      : type_(type), value_(value), byte_offset_(byte_offset) {}

  Type type() const { return type_; }
  const std::string& value() const { return value_; }

  // Offset into the input string where this token begins.
  size_t byte_offset() const { return byte_offset_; }

 private:
  Type type_ = kInvalid;
  std::string value_;
  size_t byte_offset_ = 0;
};

}  // namespace zxdb
