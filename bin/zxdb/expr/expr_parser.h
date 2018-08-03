// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_token.h"

namespace zxdb {

class ExprParser {
 public:
  ExprParser(std::vector<ExprToken> tokens);

  // Returns the root expression node on successful parsing. On error, returns
  // an empty pointer in which case the error message can be read from err()
  // ad error_token()
  std::unique_ptr<ExprNode> Parse();

  // The result of parsing.
  const Err& err() const { return err_; }

  ExprToken error_token() const { return error_token_; }

 private:
  struct DispatchInfo;

  // When recursively calling this function, call with the same precedence as
  // the current expression for left-associativity (operators evaluated from
  // left-to-right), and one less for right-associativity.
  std::unique_ptr<ExprNode> ParseExpression(int precedence);

  std::unique_ptr<ExprNode> AmpersandPrefix(const ExprToken& token);
  std::unique_ptr<ExprNode> DotOrArrowInfix(std::unique_ptr<ExprNode> left,
                                            const ExprToken& token);
  std::unique_ptr<ExprNode> IntegerPrefix(const ExprToken& token);
  std::unique_ptr<ExprNode> LeftParenPrefix(const ExprToken& token);
  std::unique_ptr<ExprNode> LeftSquareInfix(std::unique_ptr<ExprNode> left,
                                            const ExprToken& token);
  std::unique_ptr<ExprNode> MinusPrefix(const ExprToken& token);
  std::unique_ptr<ExprNode> NamePrefix(const ExprToken& token);
  std::unique_ptr<ExprNode> NameInfix(std::unique_ptr<ExprNode> left,
                                      const ExprToken& token);
  std::unique_ptr<ExprNode> StarPrefix(const ExprToken& token);

  // Returns the next token or the invalid token if nothing is left. Advances
  // to the next token.
  const ExprToken& Consume();

  // Consumes a token of the given type, returning it if there was one
  // available and the type matches. Otherwise, sets the error condition using
  // the given error_token and message, and returns a reference to an invalid
  // token. It will advance to the next token.
  const ExprToken& Consume(ExprToken::Type type, const ExprToken& error_token,
                           const char* error_msg);

  void SetError(const ExprToken& token, std::string msg);

  // Call this only if !at_end().
  const ExprToken& cur_token() const { return tokens_[cur_]; }

  bool has_error() const { return err_.has_error(); }
  bool at_end() const { return cur_ == tokens_.size(); }

  static DispatchInfo kDispatchInfo[];

  std::vector<ExprToken> tokens_;
  size_t cur_ = 0;  // Current index into tokens_.

  // On error, the message and token where an error was encountered.
  Err err_;
  ExprToken error_token_;

  // This is a kInvalid token that we can return in error cases without having
  // to reference something in the tokens_ array.
  static const ExprToken kInvalidToken;
};

}  // namespace zxdb
