// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_parser.h"

#include "garnet/bin/zxdb/expr/template_type_extractor.h"
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
// The commented-out values are ones we don't currently implement.

// clang-format off
//constexpr int kPrecedenceComma = 10;                // ,  (lowest precedence)
constexpr int kPrecedenceAssignment = 20;             // = += -= *= -= /= %= <<= >>= &= ^= |=
//constexpr int kPrecedenceLogicalOr = 30;            // ||
//constexpr int kPrecedenceLogicalAnd = 40;           // &&
//constexpr int kPrecedenceBitwiseOr = 50;            // |
//constexpr int kPrecedenceBitwiseXor = 60;           // ^
//constexpr int kPrecedenceBitwiseAnd = 70;           // &
constexpr int kPrecedenceEquality = 80;               // == !=
//constexpr int kPrecedenceComparison = 90;           // < <= > >=
//constexpr int kPrecedenceThreeWayComparison = 100;  // <=>
//constexpr int kPrecedenceShift = 110;               // << >>
//constexpr int kPrecedenceAddition = 120;            // + -
//constexpr int kPrecedenceMultiplication = 130;      // * / %
//constexpr int kPrecedencePointerToMember = 140;     // .* ->*
constexpr int kPrecedenceUnary = 150;                 // ++ -- +a -a ! ~ *a &a
constexpr int kPrecedenceCallAccess = 160;            // () . -> []
constexpr int kPrecedenceScope = 170;                 // ::  (Highest precedence)
// clang-format on

}  // namespace

struct ExprParser::DispatchInfo {
  PrefixFunc prefix = nullptr;
  InfixFunc infix = nullptr;
  int precedence = 0;  // Only needed when |infix| is set.
};

// The table is more clear without line wrapping.
// clang-format off
ExprParser::DispatchInfo ExprParser::kDispatchInfo[] = {
    {nullptr,                      nullptr,                      -1},                     // kInvalid
    {&ExprParser::NamePrefix,      &ExprParser::NameInfix,       -1},                     // kName
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kInteger
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceAssignment},  // kEquals
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceEquality},    // kEqualsEquals
    {nullptr,                      &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},  // kDot
    {nullptr,                      nullptr,                      -1},                     // kComma
    {&ExprParser::StarPrefix,      nullptr,                      kPrecedenceUnary},       // kStar
    {&ExprParser::AmpersandPrefix, nullptr,                      kPrecedenceUnary},       // kAmpersand
    {nullptr,                      &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},  // kArrow
    {nullptr,                      &ExprParser::LeftSquareInfix, kPrecedenceCallAccess},  // kLeftSquare
    {nullptr,                      nullptr,                      -1},                     // kRightSquare
    {&ExprParser::LeftParenPrefix, nullptr,                      -1},                     // kLeftParen
    {nullptr,                      nullptr,                      -1},                     // kRightParen
    {nullptr,                      &ExprParser::LessInfix,       kPrecedenceUnary},       // kLess
    {nullptr,                      nullptr,                      -1},                     // kGreater
    {&ExprParser::MinusPrefix,     nullptr,                      -1},                     // kMinus
    {nullptr,                      nullptr,                      -1},                     // kPlus (currently unhandled)
    {&ExprParser::ScopePrefix,     &ExprParser::ScopeInfix,      kPrecedenceScope},       // kColonColon
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kTrue
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kFalse
};
// clang-format on

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
    return nullptr;
  }

  if (!result && !has_error()) {
    SetError(ExprToken(), "No input to parse.");
    return nullptr;
  }
  return result;
}

fxl::RefPtr<ExprNode> ExprParser::ParseExpression(int precedence) {
  if (at_end())
    return nullptr;

  const ExprToken& token = Consume();
  PrefixFunc prefix = kDispatchInfo[token.type()].prefix;

  if (!prefix) {
    SetError(token, fxl::StringPrintf("Unexpected token '%s'.",
                                      token.value().c_str()));
    return nullptr;
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
      return nullptr;
    }
    left = (this->*infix)(std::move(left), next_token);
    if (has_error())
      return nullptr;
  }

  return left;
}

// A list is any sequence of comma-separated types. We don't parse the types
// (this is hard) but instead skip over them.
std::vector<std::string> ExprParser::ParseTemplateList(
    ExprToken::Type stop_before) {
  std::vector<std::string> result;

  while (!at_end() && !LookAhead(stop_before)) {
    TemplateTypeResult type_result = ExtractTemplateType(tokens_, cur_);
    if (!type_result.success) {
      SetError(tokens_[type_result.unmatched_error_token],
               fxl::StringPrintf(
                   "Unmatched '%s'.",
                   tokens_[type_result.unmatched_error_token].value().c_str()));
      return std::vector<std::string>();
    }
    cur_ = type_result.end_token;
    result.push_back(std::move(type_result.canonical_name));

    // Consume a comma if there is one, but don't consume anything else. The
    // end token will break out of our loop.
    if (LookAhead(ExprToken::kComma))
      Consume();
  }
  return result;
}

fxl::RefPtr<ExprNode> ExprParser::AmpersandPrefix(const ExprToken& token) {
  fxl::RefPtr<ExprNode> right = ParseExpression(kPrecedenceUnary);
  if (!has_error() && !right)
    SetError(token, "Expected expression for '&'.");
  if (has_error())
    return nullptr;
  return fxl::MakeRefCounted<AddressOfExprNode>(std::move(right));
}

fxl::RefPtr<ExprNode> ExprParser::BinaryOpInfix(fxl::RefPtr<ExprNode> left,
                                                const ExprToken& token) {
  fxl::RefPtr<ExprNode> right = ParseExpression(kPrecedenceUnary);
  if (!has_error() && !right) {
    SetError(token, fxl::StringPrintf(
                        "Expected expression after '%s'.",
                        token.value().c_str()));
  }
  if (has_error())
    return nullptr;

  return fxl::MakeRefCounted<BinaryOpExprNode>(std::move(left), token, std::move(right));
}

fxl::RefPtr<ExprNode> ExprParser::ScopeInfix(fxl::RefPtr<ExprNode> left,
                                             const ExprToken& token) {
  // Scope infix means we have two things separated by a "::". Both the right
  // and the left should be identifier nodes. Scope resolution is
  // left-associative so use the same precedence as the token.
  return JoinIdentifiers(left, token, ParseExpression(kPrecedenceScope));
}

fxl::RefPtr<ExprNode> ExprParser::ScopePrefix(const ExprToken& token) {
  // Scope prefix means we found something like "::foo". Scope resolution is
  // left-associative so use the same precedence as the token.
  return JoinIdentifiers(nullptr, token, ParseExpression(kPrecedenceScope));
}

fxl::RefPtr<ExprNode> ExprParser::DotOrArrowInfix(fxl::RefPtr<ExprNode> left,
                                                  const ExprToken& token) {
  // These are left-associative so use the same precedence as the token.
  fxl::RefPtr<ExprNode> right = ParseExpression(kPrecedenceCallAccess);
  if (!right || !right->AsIdentifier()) {
    SetError(token, fxl::StringPrintf(
                        "Expected identifier for right-hand-side of \"%s\".",
                        token.value().c_str()));
    return nullptr;
  }

  // Use the name from the right-hand-side identifier, we don't need a full
  // expression for that. If we add function calls it will be necessary.
  return fxl::MakeRefCounted<MemberAccessExprNode>(
      std::move(left), token, right->AsIdentifier()->ident());
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
    return nullptr;
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
    return nullptr;
  return fxl::MakeRefCounted<ArrayAccessExprNode>(std::move(left),
                                                  std::move(inner));
}

fxl::RefPtr<ExprNode> ExprParser::LessInfix(fxl::RefPtr<ExprNode> left,
                                            const ExprToken& token) {
  std::vector<std::string> list = ParseTemplateList(ExprToken::kGreater);
  if (has_error())
    return nullptr;

  // Ending ">".
  const ExprToken& template_end =
      Consume(ExprToken::kGreater, token, "Expected '>' to match.");
  if (has_error())
    return nullptr;

  // For templates the thing on the left should always be an identifier.
  const IdentifierExprNode* left_ident_node = left->AsIdentifier();
  if (!left_ident_node) {
    SetError(token, "Unexpected '<'.");
    return nullptr;
  }

  // Find what we're adding the template to.
  const auto& comps = left_ident_node->ident().components();
  FXL_DCHECK(!comps.empty());
  if (comps.back().has_template()) {
    SetError(token, "Duplicate template specification.");
    return nullptr;
  }
  const auto& back = comps.back();

  // Make a new identifier with the template appended to the last component.
  auto result = fxl::MakeRefCounted<IdentifierExprNode>();
  result->ident() = left_ident_node->ident();
  result->ident().components().back() = Identifier::Component(
      back.separator(), back.name(), token, std::move(list), template_end);
  return result;
}

fxl::RefPtr<ExprNode> ExprParser::LiteralPrefix(const ExprToken& token) {
  return fxl::MakeRefCounted<LiteralExprNode>(token);
}

fxl::RefPtr<ExprNode> ExprParser::GreaterInfix(fxl::RefPtr<ExprNode> left,
                                               const ExprToken& token) {
  return nullptr;
}

fxl::RefPtr<ExprNode> ExprParser::MinusPrefix(const ExprToken& token) {
  // Currently we only implement "-" as a prefix which is for unary "-" when
  // you type "-5" or "-foo[6]". An infix version would be needed to parse the
  // binary operator for "a - 6".
  auto inner = ParseExpression(kPrecedenceUnary);
  if (!has_error() && !inner)
    SetError(token, "Expected expression for '-'.");
  if (has_error())
    return nullptr;
  return fxl::MakeRefCounted<UnaryOpExprNode>(token, std::move(inner));
}

fxl::RefPtr<ExprNode> ExprParser::NamePrefix(const ExprToken& token) {
  return NameInfix(nullptr, token);
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
    return nullptr;
  return fxl::MakeRefCounted<DereferenceExprNode>(std::move(right));
}

bool ExprParser::LookAhead(ExprToken::Type type) const {
  if (at_end())
    return false;
  return cur_token().type() == type;
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
             std::string(error_msg) + " Hit the end of input instead.");
    return kInvalidToken;
  }

  if (cur_token().type() == type)
    return Consume();

  SetError(error_token, error_msg);
  return kInvalidToken;
}

fxl::RefPtr<ExprNode> ExprParser::JoinIdentifiers(fxl::RefPtr<ExprNode> left,
                                                  const ExprToken& scope_token,
                                                  fxl::RefPtr<ExprNode> right) {
  // This always makes a new identifier ExprNode. In most cases this is
  // unnecessary and we could easily modify left or right, or at least suck out
  // the Components to avoid copying all the strings. But this is not
  // performance-critical so the simpler solution is better.
  auto result = fxl::MakeRefCounted<IdentifierExprNode>();

  // Left identifier is optional since this is also used to prefix things with
  // a "::".
  if (left) {
    if (const IdentifierExprNode* left_ident = left->AsIdentifier()) {
      result->ident() = left_ident->ident();
    } else {
      SetError(scope_token, "'::' not expected here.");
      return nullptr;
    }
  }

  // Right side is required.
  const IdentifierExprNode* right_ident = nullptr;
  if (!right || !(right_ident = right->AsIdentifier())) {
    SetError(scope_token, "Expected identifier after '::'.");
    return nullptr;
  }

  // All identifiers should have >= one component.
  const auto& right_comps = right_ident->ident().components();
  FXL_DCHECK(!right_comps.empty());

  // Right shouldn't start with a "::" since we're adding another one.
  const Identifier::Component& first_comp = right_comps[0];
  if (first_comp.has_separator()) {
    // Two "::" in a row.
    SetError(first_comp.separator(), "Duplicate '::'.");
    return nullptr;
  }

  // Append all the right elements, adding the new scope to the first.
  result->ident().components().emplace_back(
      scope_token, first_comp.name(), first_comp.template_begin(),
      first_comp.template_contents(), first_comp.template_end());
  for (size_t i = 1; i < right_comps.size(); i++)
    result->ident().components().push_back(right_comps[i]);

  return result;
}

void ExprParser::SetError(const ExprToken& token, std::string msg) {
  err_ = Err(std::move(msg));
  error_token_ = token;
}

}  // namespace zxdb
