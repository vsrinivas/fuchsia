// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_parser.h"

#include "garnet/bin/zxdb/expr/name_lookup.h"
#include "garnet/bin/zxdb/expr/template_type_extractor.h"
#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

// The parser is a Pratt parser. The basic idea there is to have the
// precedences (and associativities) encoded relative to each other and only
// parse up until you hit something of that precedence. There's a dispatch
// table in kDispatchInfo that describes how each token dispatches if it's seen
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
constexpr int kPrecedenceComma = 10;                  // ,  (lowest precedence)
constexpr int kPrecedenceAssignment = 20;             // = += -= *= -= /= %= <<= >>= &= ^= |=
constexpr int kPrecedenceLogicalOr = 30;              // ||
constexpr int kPrecedenceLogicalAnd = 40;             // &&
constexpr int kPrecedenceBitwiseOr = 50;              // |
//constexpr int kPrecedenceBitwiseXor = 60;           // ^
constexpr int kPrecedenceBitwiseAnd = 70;             // &
constexpr int kPrecedenceEquality = 80;               // == !=
//constexpr int kPrecedenceComparison = 90;           // < <= > >=
//constexpr int kPrecedenceThreeWayComparison = 100;  // <=>
//constexpr int kPrecedenceShift = 110;               // << >>
//constexpr int kPrecedenceAddition = 120;            // + -
//constexpr int kPrecedenceMultiplication = 130;      // * / %
//constexpr int kPrecedencePointerToMember = 140;     // .* ->*
constexpr int kPrecedenceUnary = 150;                 // ++ -- +a -a ! ~ *a &a
constexpr int kPrecedenceCallAccess = 160;            // () . -> []
//constexpr int kPrecedenceScope = 170;               // ::  (Highest precedence)
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
    {&ExprParser::NamePrefix,      nullptr,                      -1},                     // kName
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kInteger
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceAssignment},  // kEquals
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceEquality},    // kEqualsEquals
    {nullptr,                      &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},  // kDot
    {nullptr,                      nullptr,                      -1},                     // kComma
    {&ExprParser::StarPrefix,      nullptr,                      kPrecedenceUnary},       // kStar
    {&ExprParser::AmpersandPrefix, &ExprParser::BinaryOpInfix,   kPrecedenceBitwiseAnd},  // kAmpersand
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceLogicalAnd},  // kDoubleAnd
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceBitwiseOr},   // kBitwiseOr
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceLogicalOr},   // kLogicalOr
    {nullptr,                      &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},  // kArrow
    {nullptr,                      &ExprParser::LeftSquareInfix, kPrecedenceCallAccess},  // kLeftSquare
    {nullptr,                      nullptr,                      -1},                     // kRightSquare
    {&ExprParser::LeftParenPrefix, &ExprParser::LeftParenInfix,  kPrecedenceCallAccess},  // kLeftParen
    {nullptr,                      nullptr,                      -1},                     // kRightParen
    {nullptr,                      &ExprParser::LessInfix,       kPrecedenceUnary},       // kLess
    {nullptr,                      nullptr,                      -1},                     // kGreater
    {&ExprParser::MinusPrefix,     nullptr,                      -1},                     // kMinus
    {nullptr,                      nullptr,                      -1},                     // kPlus (currently unhandled)
    {&ExprParser::NamePrefix,      nullptr,                      -1},                     // kColonColon
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kTrue
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kFalse
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kConst
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kVolatile
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                     // kRestrict
};
// clang-format on

// static
const ExprToken ExprParser::kInvalidToken;

ExprParser::ExprParser(std::vector<ExprToken> tokens,
                       NameLookupCallback name_lookup)
    : name_lookup_callback_(std::move(name_lookup)),
      tokens_(std::move(tokens)) {
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
      SetError(next_token, fxl::StringPrintf("Unexpected token '%s'.",
                                             next_token.value().c_str()));
      return nullptr;
    }
    left = (this->*infix)(std::move(left), next_token);
    if (has_error())
      return nullptr;
  }

  return left;
}

ExprParser::ParseNameResult ExprParser::ParseName() {
  // Grammar we support. Note "identifier" in this context is a single token
  // of type "name" (more like how the C++ spec uses it), while our Identifier
  // class represents a whole name with scopes and templates.
  //
  //   name := type-name | non-type-identifier
  //
  //   type-name :=
  //       [ type-name "::" ] identifier [ "<" template-list ">" ]
  //       "::" identifier [ "<" template-list ">" ]
  //
  //   non-type-identifier := [ <type-name> "::" ] <identifier>
  //
  // The thing this doesn't handle is templatized functions, for example:
  //   auto foo = &MyClass::MyFunc<int>;
  // To handle this we will need the type lookup function to be able to tell
  // us "MyClass::MyFunc" is a thing that has a template so we know to parse
  // the following "<" as part of the name and not as a comparison. Note that
  // when we need to parse function names, there is special handling required
  // for operators.

  // The mode of the state machine.
  enum Mode {
    kBegin,       // Inital state with no previous context.
    kColonColon,  // Just saw a "::", expecting a name next.
    kType,        // Idenfitier is a type.
    kTemplate,    // Idenfitier is a template, expecting "<" next.
    kNamespace,   // Identifier is a namespace.
    kOtherName,   // Identifier is something other than the above (normally this
                  // means a variable).
    kAnything     // Caller can't do symbol lookups, accept anything that makes
                  // sense.
  };

  Mode mode = kBegin;
  ParseNameResult result;
  const ExprToken* prev_token = nullptr;

  while (!at_end()) {
    const ExprToken& token = cur_token();
    switch (token.type()) {
      case ExprToken::kColonColon: {
        // "::" can only follow nothing, a namespace or type name.
        if (mode != kBegin && mode != kNamespace && mode != kType &&
            mode != kAnything) {
          SetError(token,
                   "Could not identify thing to the left of '::' as a type or "
                   "namespace.");
          return ParseNameResult();
        }

        mode = kColonColon;
        result.ident.AppendComponent(token, ExprToken());  // Append "::".
        result.type = nullptr;                             // No longer a type.
        break;
      }

      case ExprToken::kLess: {
        // "<" can only come after a template name.
        if (mode == kNamespace || mode == kType) {
          // Generate a nicer error for these cases.
          SetError(token, "Template parameters not valid on this object type.");
          return ParseNameResult();
        }
        if (mode != kTemplate && mode != kAnything) {
          // "<" after anything but a template means the end of the name. In
          // "anything" mode we assume "<" means a template since this is used
          // to parse random identifiers and function names.
          return result;
        }
        if (result.ident.components().back().has_template()) {
          // Got a "<" after a template parameter list was already defined
          // (this will happen in "anything" mode since we don't know what it
          // is for sure). That means this is a comparison operator which will
          // be handled by the outer parser.
          return result;
        }

        prev_token = &Consume();  // Eat the "<".

        // Extract the contents of the template.
        std::vector<std::string> list = ParseTemplateList(ExprToken::kGreater);
        if (has_error())
          return ParseNameResult();

        // Ending ">".
        const ExprToken& template_end =
            Consume(ExprToken::kGreater, token, "Expected '>' to match.");
        if (has_error())
          return ParseNameResult();

        // Construct a replacement for the last component of the identifier
        // with the template arguments added.
        Identifier::Component& back = result.ident.components().back();
        back = Identifier::Component(back.separator(), back.name(), token,
                                     std::move(list), template_end);

        // The thing we just made is either a type or a name, look it up.
        if (name_lookup_callback_) {
          NameLookupResult lookup = name_lookup_callback_(result.ident);
          switch (lookup.kind) {
            case NameLookupResult::kType:
              mode = kType;
              result.type = std::move(lookup.type);
              break;
            case NameLookupResult::kNamespace:
            case NameLookupResult::kTemplate:
              // The lookup shouldn't tell us a template name or namespace for
              // something that has template parameters.
              FXL_NOTREACHED();
              // Fall through to "other" case for fallback.
            case NameLookupResult::kOther:
              mode = kOtherName;
              break;
          }
        } else {
          mode = kAnything;
        }
        continue;  // Don't Consume() since we already ate the token.
      }

      case ExprToken::kName: {
        // Names can only follow nothing or "::".
        if (mode == kType) {
          // Normally in C++ a name can follow a type, so make a special error
          // for this case.
          SetError(token,
                   "This looks like a declaration which is not supported.");
          return ParseNameResult();
        } else if (mode == kBegin) {
          // Found an identifier name with nothing before it.
          result.ident = Identifier(token);
        } else if (mode == kColonColon) {
          FXL_DCHECK(!result.ident.components().empty());
          result.ident.components().back().set_name(cur_token());
        } else {
          // Anything else like "std::vector foo" or "foo bar".
          SetError(token, "Unexpected identifier, did you forget an operator?");
          return ParseNameResult();
        }

        // Decode what adding the name just generated.
        if (name_lookup_callback_) {
          NameLookupResult lookup = name_lookup_callback_(result.ident);
          switch (lookup.kind) {
            case NameLookupResult::kNamespace:
              mode = kNamespace;
              break;
            case NameLookupResult::kTemplate:
              mode = kTemplate;
              break;
            case NameLookupResult::kType:
              mode = kType;
              result.type = std::move(lookup.type);
              break;
            case NameLookupResult::kOther:
              mode = kOtherName;
              break;
          }
        } else {
          mode = kAnything;
        }
        break;
      }

      default: {
        // Any other token type means we're done. The outer parser will figure
        // out what it means.
        return result;
      }
    }
    prev_token = &Consume();
  }

  // Hit end-of-input.
  switch (mode) {
    case kOtherName:
    case kAnything:
    case kType:
      return result;  // Success cases.
    case kBegin:
      FXL_NOTREACHED();
      SetError(ExprToken(), "Unexpected end of input.");
      return ParseNameResult();
    case kColonColon:
      SetError(*prev_token, "Expected name after '::'.");
      return ParseNameResult();
    case kTemplate:
      SetError(*prev_token, "Expected template args after template name.");
      return ParseNameResult();
    case kNamespace:
      SetError(*prev_token, "Expected expression after namespace name.");
      return ParseNameResult();
  }

  FXL_NOTREACHED();
  SetError(ExprToken(), "Internal error.");
  return ParseNameResult();
}

// A list is any sequence of comma-separated types. We don't parse the types
// (this is hard) but instead skip over them.
std::vector<std::string> ExprParser::ParseTemplateList(
    ExprToken::Type stop_before) {
  std::vector<std::string> result;

  bool first_time = true;
  while (!at_end() && !LookAhead(stop_before)) {
    if (first_time) {
      first_time = false;
    } else {
      // Expect comma to separate items.
      if (LookAhead(ExprToken::kComma)) {
        Consume();
      } else {
        SetError(cur_token(), "Expected ',' separating expressions.");
        return {};
      }
    }

    TemplateTypeResult type_result = ExtractTemplateType(tokens_, cur_);
    if (!type_result.success) {
      SetError(tokens_[type_result.unmatched_error_token],
               fxl::StringPrintf(
                   "Unmatched '%s'.",
                   tokens_[type_result.unmatched_error_token].value().c_str()));
      return {};
    } else if (cur_ == type_result.end_token) {
      SetError(cur_token(), "Expected template parameter.");
      return {};
    }
    cur_ = type_result.end_token;
    result.push_back(std::move(type_result.canonical_name));
  }
  return result;
}

// This function is called in contexts where we expect a comma-separated list.
// Currently these are all known in advance so this simple manual parsing will
// do. A more general approach would implement a comma infix which constructs a
// new type of ExprNode.
std::vector<fxl::RefPtr<ExprNode>> ExprParser::ParseExpressionList(
    ExprToken::Type stop_before) {
  std::vector<fxl::RefPtr<ExprNode>> result;

  bool first_time = true;
  while (!at_end() && !LookAhead(stop_before)) {
    if (first_time) {
      first_time = false;
    } else {
      // Expect comma to separate items.
      if (LookAhead(ExprToken::kComma)) {
        Consume();
      } else {
        SetError(cur_token(), "Expected ',' separating expressions.");
        return {};
      }
    }

    fxl::RefPtr<ExprNode> cur = ParseExpression(kPrecedenceComma);
    if (has_error())
      return {};
    result.push_back(std::move(cur));
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
  const DispatchInfo& dispatch = kDispatchInfo[token.type()];
  fxl::RefPtr<ExprNode> right = ParseExpression(dispatch.precedence);
  if (!has_error() && !right) {
    SetError(token, fxl::StringPrintf("Expected expression after '%s'.",
                                      token.value().c_str()));
  }
  if (has_error())
    return nullptr;

  return fxl::MakeRefCounted<BinaryOpExprNode>(std::move(left), token,
                                               std::move(right));
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

fxl::RefPtr<ExprNode> ExprParser::LeftParenInfix(fxl::RefPtr<ExprNode> left,
                                                 const ExprToken& token) {
  // "(" as an infix is a function call. In this case, expect the thing on the
  // left to be an identifier which is the name of the function.
  const IdentifierExprNode* left_ident_node = left->AsIdentifier();
  if (!left_ident_node) {
    SetError(token, "Unexpected '('.");
    return nullptr;
  }
  // Const cast is required because the type conversions only have const
  // versions, although our object is not const.
  Identifier name =
      const_cast<IdentifierExprNode*>(left_ident_node)->TakeIdentifier();

  // Read the function parameters.
  std::vector<fxl::RefPtr<ExprNode>> args =
      ParseExpressionList(ExprToken::Type::kRightParen);
  if (has_error())
    return nullptr;
  Consume(ExprToken::kRightParen, token, "Expected ')' to match.");

  return fxl::MakeRefCounted<FunctionCallExprNode>(std::move(name),
                                                   std::move(args));
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
  SetError(token, "Comparisons not supported yet.");
  return nullptr;
}

fxl::RefPtr<ExprNode> ExprParser::LiteralPrefix(const ExprToken& token) {
  return fxl::MakeRefCounted<LiteralExprNode>(token);
}

fxl::RefPtr<ExprNode> ExprParser::GreaterInfix(fxl::RefPtr<ExprNode> left,
                                               const ExprToken& token) {
  SetError(token, "Comparisons not supported yet.");
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
  // Handles names and "::" which precedes names. This could be a typename
  // ("int", or "::std::vector<int>") or a variable name ("i",
  // "std::basic_string<char>::npos").

  // Back up so the current token is the first component of the name so we
  // can hand-off to the specialized name parser.
  FXL_DCHECK(cur_ > 0);
  cur_--;

  // TODO(brettw) handle const/volatile/restrict here to force type
  // parsing mode.

  ParseNameResult result = ParseName();
  if (has_error())
    return nullptr;

  if (result.type) {
    // TODO(brettw) go into type parsing mode.
    SetError(token, "Type, implement me.");
    return nullptr;
  }

  // Normal identifier.
  return fxl::MakeRefCounted<IdentifierExprNode>(std::move(result.ident));
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

void ExprParser::SetError(const ExprToken& token, std::string msg) {
  err_ = Err(std::move(msg));
  error_token_ = token;
}

}  // namespace zxdb
