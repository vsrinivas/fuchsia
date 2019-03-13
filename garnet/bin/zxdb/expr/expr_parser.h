// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_token.h"
#include "garnet/bin/zxdb/expr/name_lookup.h"

namespace zxdb {

class ExprParser {
 public:
  // The name lookup callback can be empty if the caller doesn't have any
  // symbol context. This means that we can't disambiguate some cases like how
  // to parse "Foo < 1 > bar". In this mode, we'll assume that "<" after a
  // name always mean a template rather than a comparison operation.
  ExprParser(std::vector<ExprToken> tokens,
             NameLookupCallback name_lookup = NameLookupCallback());

  // Returns the root expression node on successful parsing. On error, returns
  // an empty pointer in which case the error message can be read from err()
  // ad error_token()
  fxl::RefPtr<ExprNode> Parse();

  // The result of parsing. Since this does not have access to the initial
  // string, it will not indicate context for the error. That can be generated
  // from the error_token() if desired.
  const Err& err() const { return err_; }

  ExprToken error_token() const { return error_token_; }

 private:
  struct DispatchInfo;

  // When recursively calling this function, call with the same precedence as
  // the current expression for left-associativity (operators evaluated from
  // left-to-right), and one less for right-associativity.
  fxl::RefPtr<ExprNode> ParseExpression(int precedence);

  // Parses the name of a symbol or a non-type identifier (e.g. a variable
  // name) starting at cur_token().
  //
  // This is separate from the regular parsing to simplify the structure. These
  // names can be parsed linearly (we don't go into templates which is where
  // recursion comes in) so the implementation is more straightforward, and
  // it's nicer to get the handling out of the general "<" token handler, for
  // example.
  //
  // On error, has_error() will be set and an empty ParseNameResult will be
  // returned (with empty identifier and type).
  struct ParseNameResult {
    ParseNameResult() = default;

    // On success, always contains the identifier name.
    Identifier ident;

    // When the result is a type, this will contain the resolved type. When
    // null, the result is a non-type or an error.
    fxl::RefPtr<Type> type;
  };
  ParseNameResult ParseName();

  // Parses template parameter lists. The "stop_before" parameter indicates how
  // the list is expected to end (i.e. ">"). Sets the error on failure.
  std::vector<std::string> ParseTemplateList(ExprToken::Type stop_before);

  // Parses comma-separated lists of expressions. Runs until the given ending
  // token is found (normally a ')' for a function call).
  std::vector<fxl::RefPtr<ExprNode>> ParseExpressionList(
      ExprToken::Type stop_before);

  fxl::RefPtr<ExprNode> AmpersandPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> BinaryOpInfix(fxl::RefPtr<ExprNode> left,
                                      const ExprToken& token);
  fxl::RefPtr<ExprNode> DotOrArrowInfix(fxl::RefPtr<ExprNode> left,
                                        const ExprToken& token);
  fxl::RefPtr<ExprNode> LeftParenPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> LeftParenInfix(fxl::RefPtr<ExprNode> left,
                                       const ExprToken& token);
  fxl::RefPtr<ExprNode> LeftSquareInfix(fxl::RefPtr<ExprNode> left,
                                        const ExprToken& token);
  fxl::RefPtr<ExprNode> LessInfix(fxl::RefPtr<ExprNode> left,
                                  const ExprToken& token);
  fxl::RefPtr<ExprNode> LiteralPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> GreaterInfix(fxl::RefPtr<ExprNode> left,
                                     const ExprToken& token);
  fxl::RefPtr<ExprNode> MinusPrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> NamePrefix(const ExprToken& token);
  fxl::RefPtr<ExprNode> StarPrefix(const ExprToken& token);

  // Returns true if the next token is the given type.
  bool LookAhead(ExprToken::Type type) const;

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

  // Possibly null, see constructor.
  NameLookupCallback name_lookup_callback_;

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
