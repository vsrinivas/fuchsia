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
  enum Type {
    kInvalid,
    kName,         // random_text
    kInteger,      // 123
    kDot,          // .
    kStar,         // *
    kAmpersand,    // &
    kArrow,        // ->
    kLeftSquare,   // [
    kRightSquare,  // ]
    kLeftParen,    // (
    kRightParen,   // )
    kMinus,        // - (by itself, not part of "->")

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
