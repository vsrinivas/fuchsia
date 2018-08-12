// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_token.h"

namespace zxdb {

class ExprTokenizer {
 public:
  explicit ExprTokenizer(const std::string& input);

  // Returns true on successful tokenizing. In this case, the tokens can be
  // read from tokens(). On failure, err() will contain the error message, and
  // error_location() will contain the error location.
  bool Tokenize();

  const std::string& input() const { return input_; }

  // The result of parsing. This will be multiline and will indicate the
  // location of the problem.
  const Err& err() const { return err_; }

  // When err is set, this will be the index into the input() string where the
  // error occurred.
  size_t error_location() const { return error_location_; }

  // When parsing is successful, this contains the extracted tokens.
  const std::vector<ExprToken>& tokens() const { return tokens_; }

  std::vector<ExprToken> TakeTokens() { return std::move(tokens_); }

  // Returns two context lines for an error message. It will quote a relevant
  // portion of the input showing the byte offset, and add a "^" on the next
  // line to indicate where the error is.
  static std::string GetErrorContext(const std::string& input,
                                     size_t byte_offset);

 private:
  void AdvanceOneChar();
  void AdvanceToNextToken();
  void AdvanceToEndOfToken(ExprToken::Type type);

  bool IsCurrentWhitespace() const;
  ExprToken::Type ClassifyCurrent();

  bool done() const { return at_end() || has_error(); }
  bool has_error() const { return err_.has_error(); }
  bool at_end() const { return cur_ == input_.size(); }
  char cur_char() const { return input_[cur_]; }

  std::string input_;
  size_t cur_ = 0;  // Character offset into input_.

  Err err_;
  size_t error_location_ = 0;

  std::vector<ExprToken> tokens_;
};

}  // namespace zxdb
