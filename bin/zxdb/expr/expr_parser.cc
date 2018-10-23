// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_parser.h"

#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

// The parser is a Pratt parser. The basic idea there is to have the
// precedences (and associativities) encoded relative to each other and only
// parse up until you hit something of that precedence. There's a dispatch
// table in expressions_ that describes how each token dispatches if it's seen
// as either a prefix or infix operator, and if it's infix, what its precedence
// is.
//
// References:
// http://javascript.crockford.com/tdop/tdop.html
// http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/

namespace zxdb {

namespace {

// An infix operator is one that combines two sides of things and it modifies
// both, like "a + b" ("a" is the "left" and "+" is the token in the params).
//
// Other things are infix like "[" which combines the expression on the left
// with some expression to the right of it.
//
// A prefix operator are binary operators like "!" in C that only apply to the
// thing on the right and don't require anything on the left. Standalone
// numbers and names are also considered prefix since they represent themselves
// (not requiring anything on the left).
//
// Some things can be both prefix and infix. An example in C is "(" which is
// prefix when used in casts and math expressions: "(a + b)" "a + (b + c)" but
// infix when used for function calls: "foo(bar)".
using PrefixFunc = fxl::RefPtr<ExprNode> (ExprParser::*)(const ExprToken&);
using InfixFunc = fxl::RefPtr<ExprNode> (ExprParser::*)(
    fxl::RefPtr<ExprNode> left, const ExprToken& token);

// Precedence constants used in DispatchInfo. Note that these aren't
// contiguous. At least need to do every-other-one to handle the possible
// "precedence - 1" that occurs when evaluating right-associative operators. We
// don't want that operation to push the precedence into a completely other
// category, rather, it should only affect comparisons that would otherwise be
// equal.
//
// This should match the C operator precedence for the subset of operations
// that we support:
//   https://en.cppreference.com/w/cpp/language/operator_precedence

// Lowest precedence: Most C unary operators like "*", "&", and "-".
constexpr int kPrecedenceUnary = 10;

// Highest precedence: () . -> []: Highest precedence.
constexpr int kPrecedenceCallAccess = 30;

}  // namespace

struct ExprParser::DispatchInfo {
  PrefixFunc prefix = nullptr;
  InfixFunc infix = nullptr;
  int precedence = 0;
};

ExprParser::DispatchInfo ExprParser::kDispatchInfo[] = {
    {nullptr, nullptr, -1},                                          // kInvalid
    {&ExprParser::NamePrefix, &ExprParser::NameInfix, -1},           // kName
    {&ExprParser::IntegerPrefix, nullptr, -1},                       // kInteger
    {nullptr, &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},  // kDot
    {&ExprParser::StarPrefix, nullptr, kPrecedenceUnary},            // kStar
    {&ExprParser::AmpersandPrefix, nullptr, kPrecedenceUnary},  // kAmpersand
    {nullptr, &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},  // kArrow
    {nullptr, &ExprParser::LeftSquareInfix,
     kPrecedenceCallAccess},                      // kLeftSquare
    {nullptr, nullptr, -1},                       // kRightSquare
    {&ExprParser::LeftParenPrefix, nullptr, -1},  // kLeftParen
    {nullptr, nullptr, -1},                       // kRightParen
    {&ExprParser::MinusPrefix, nullptr, -1},      // kMinus
};

// static
const ExprToken ExprParser::kInvalidToken;

ExprParser::ExprParser(std::vector<ExprToken> tokens)
    : tokens_(std::move(tokens)) {
  static_assert(arraysize(ExprParser::kDispatchInfo) == ExprToken::kNumTypes,
                "kDispatchInfo needs updating to match ExprToken::Type");
}

fxl::RefPtr<ExprNode> ExprParser::Parse() {
  auto result = ParseExpression(0);

  // That should have consumed everything, as we don't support multiple
  // expressions being next to each other (probably the user forgot an operator
  // and wrote something like "foo 5"
  if (!has_error() && !at_end()) {
    SetError(cur_token(), "Unexpected input, did you forget an operator?");
    return fxl::RefPtr<ExprNode>();
  }

  if (!result && !has_error()) {
    SetError(ExprToken(), "No input to parse.");
    return fxl::RefPtr<ExprNode>();
  }
  return result;
}

fxl::RefPtr<ExprNode> ExprParser::ParseExpression(int precedence) {
  if (at_end())
    return fxl::RefPtr<ExprNode>();

  const ExprToken& token = Consume();
  PrefixFunc prefix = kDispatchInfo[token.type()].prefix;

  if (!prefix) {
    SetError(token, fxl::StringPrintf("Unexpected token '%s'.",
                                      token.value().c_str()));
    return fxl::RefPtr<ExprNode>();
  }

  fxl::RefPtr<ExprNode> left = (this->*prefix)(token);
  if (has_error())
    return left;

  while (!at_end() &&
         precedence < kDispatchInfo[cur_token().type()].precedence) {
    const ExprToken& next_token = Consume();
    InfixFunc infix = kDispatchInfo[next_token.type()].infix;
    if (!infix) {
      SetError(token, fxl::StringPrintf("Unexpected token '%s'.",
                                        next_token.value().c_str()));
      return fxl::RefPtr<ExprNode>();
    }
    left = (this->*infix)(std::move(left), next_token);
    if (has_error())
      return fxl::RefPtr<ExprNode>();
  }

  return left;
}

fxl::RefPtr<ExprNode> ExprParser::AmpersandPrefix(const ExprToken& token) {
  fxl::RefPtr<ExprNode> right = ParseExpression(kPrecedenceUnary);
  if (!has_error() && !right)
    SetError(token, "Expected expression for '*'.");
  if (has_error())
    return fxl::RefPtr<ExprNode>();
  return fxl::MakeRefCounted<AddressOfExprNode>(std::move(right));
}

fxl::RefPtr<ExprNode> ExprParser::DotOrArrowInfix(fxl::RefPtr<ExprNode> left,
                                                  const ExprToken& token) {
  // These are left-associative.
  fxl::RefPtr<ExprNode> right = ParseExpression(kPrecedenceCallAccess);
  if (!right || !right->AsIdentifier()) {
    SetError(token, fxl::StringPrintf(
                        "Expected identifier for right-hand-side of \"%s\".",
                        token.value().c_str()));
    return fxl::RefPtr<ExprNode>();
  }

  // Use the name from the right-hand-side identifier, we don't need a full
  // expression for that. If we add function calls it will be necessary.
  return fxl::MakeRefCounted<MemberAccessExprNode>(
      std::move(left), token, right->AsIdentifier()->name());
}

fxl::RefPtr<ExprNode> ExprParser::IntegerPrefix(const ExprToken& token) {
  return fxl::MakeRefCounted<IntegerExprNode>(token);
}

fxl::RefPtr<ExprNode> ExprParser::LeftParenPrefix(const ExprToken& token) {
  // "(" as a prefix is a grouping or cast: "a + (b + c)" or "(Foo)bar" where
  // it doesn't modify the thing on the left. Evaluate the thing inside the
  // () and return it.
  //
  // Currently there's no infix version of "(" which would be something like
  // a function call.
  auto expr = ParseExpression(0);
  if (!has_error() && !expr)
    SetError(token, "Expected expression inside '('.");
  if (!has_error())
    Consume(ExprToken::kRightParen, token, "Expected ')' to match.");
  if (has_error())
    return fxl::RefPtr<ExprNode>();
  return expr;
}

fxl::RefPtr<ExprNode> ExprParser::LeftSquareInfix(fxl::RefPtr<ExprNode> left,
                                                  const ExprToken& token) {
  auto inner = ParseExpression(0);
  if (!has_error() && !inner)
    SetError(token, "Expected expression inside '['.");
  if (!has_error())
    Consume(ExprToken::kRightSquare, token, "Expected ']' to match.");
  if (has_error())
    return fxl::RefPtr<ExprNode>();
  return fxl::MakeRefCounted<ArrayAccessExprNode>(std::move(left),
                                                  std::move(inner));
}

fxl::RefPtr<ExprNode> ExprParser::MinusPrefix(const ExprToken& token) {
  // Currently we only implement "-" as a prefix which is for unary "-" when
  // you type "-5" or "-foo[6]". An infix version would be needed to parse the
  // binary operator for "a - 6".
  auto inner = ParseExpression(kPrecedenceUnary);
  if (!has_error() && !inner)
    SetError(token, "Expected expression for '-'.");
  if (has_error())
    return fxl::RefPtr<ExprNode>();
  return fxl::MakeRefCounted<UnaryOpExprNode>(token, std::move(inner));
}

fxl::RefPtr<ExprNode> ExprParser::NamePrefix(const ExprToken& token) {
  return NameInfix(fxl::RefPtr<ExprNode>(), token);
}

fxl::RefPtr<ExprNode> ExprParser::NameInfix(fxl::RefPtr<ExprNode> left,
                                            const ExprToken& token) {
  return fxl::MakeRefCounted<IdentifierExprNode>(token);
}

fxl::RefPtr<ExprNode> ExprParser::StarPrefix(const ExprToken& token) {
  fxl::RefPtr<ExprNode> right = ParseExpression(kPrecedenceUnary);
  if (!has_error() && !right)
    SetError(token, "Expected expression for '*'.");
  if (has_error())
    return fxl::RefPtr<ExprNode>();
  return fxl::MakeRefCounted<DereferenceExprNode>(std::move(right));
}

const ExprToken& ExprParser::Consume() {
  if (at_end())
    return kInvalidToken;
  return tokens_[cur_++];
}

const ExprToken& ExprParser::Consume(ExprToken::Type type,
                                     const ExprToken& error_token,
                                     const char* error_msg) {
  FXL_DCHECK(!has_error());  // Should have error-checked before calling.
  if (at_end()) {
    SetError(error_token,
             std::string(error_msg) + " Hit the end if input instead.");
    return kInvalidToken;
  }

  if (cur_token().type() == type)
    return Consume();

  SetError(error_token, error_msg);
  return kInvalidToken;
}

void ExprParser::SetError(const ExprToken& token, std::string msg) {
  err_ = Err(std::move(msg));
  error_token_ = token;
}

}  // namespace zxdb
