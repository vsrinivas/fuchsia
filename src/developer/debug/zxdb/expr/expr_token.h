// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_H_

#include <string>

#include "src/developer/debug/zxdb/expr/expr_token_type.h"

namespace zxdb {

// A parsed token. This token does not own the strings, it's intended to be used only as an
// intermediate representation pointing into the string being parsed.
class ExprToken {
 public:
  ExprToken() = default;
  ExprToken(ExprTokenType type, const std::string& value, size_t byte_offset)
      : type_(type), value_(value), byte_offset_(byte_offset) {}

  ExprTokenType type() const { return type_; }
  const std::string& value() const { return value_; }

  // Offset into the input string where this token begins.
  size_t byte_offset() const { return byte_offset_; }

 private:
  ExprTokenType type_ = ExprTokenType::kInvalid;
  std::string value_;
  size_t byte_offset_ = 0;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_TOKEN_H_
