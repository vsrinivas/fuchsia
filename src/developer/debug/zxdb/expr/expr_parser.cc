// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_parser.h"

#include <algorithm>

#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"
#include "src/developer/debug/zxdb/expr/name_lookup.h"
#include "src/developer/debug/zxdb/expr/number_parser.h"
#include "src/developer/debug/zxdb/expr/template_type_extractor.h"
#include "src/developer/debug/zxdb/symbols/array_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/symbol_utils.h"
#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

// The parser is a Pratt parser. The basic idea there is to have the precedences (and
// associativities) encoded relative to each other and only parse up until you hit something of that
// precedence. There's a dispatch table in kDispatchInfo that describes how each token dispatches if
// it's seen as either a prefix or infix operator, and if it's infix, what its precedence is.
//
// References:
// http://javascript.crockford.com/tdop/tdop.html
// http://journal.stuffwithstuff.com/2011/03/19/pratt-parsers-expression-parsing-made-easy/

namespace zxdb {

namespace {

// An infix operator is one that combines two sides of things and it modifies both, like "a + b"
// ("a" is the "left" and "+" is the token in the params).
//
// Other things are infix like "[" which combines the expression on the left with some expression to
// the right of it.
//
// A prefix operator are binary operators like "!" in C that only apply to the thing on the right
// and don't require anything on the left. Standalone numbers and names are also considered prefix
// since they represent themselves (not requiring anything on the left).
//
// Some things can be both prefix and infix. An example in C is "(" which is prefix when used in
// casts and math expressions: "(a + b)" "a + (b + c)" but infix when used for function calls:
// "foo(bar)".
using PrefixFunc = fxl::RefPtr<ExprNode> (ExprParser::*)(const ExprToken&);
using InfixFunc = fxl::RefPtr<ExprNode> (ExprParser::*)(fxl::RefPtr<ExprNode> left,
                                                        const ExprToken& token);

// Precedence constants used in DispatchInfo. Note that these aren't contiguous. At least need to do
// every-other-one to handle the possible "precedence - 1" that occurs when evaluating
// right-associative operators. We don't want that operation to push the precedence into a
// completely other category, rather, it should only affect comparisons that would otherwise be
// equal.
//
// This should match the C operator precedence for the subset of operations that we support:
//   https://en.cppreference.com/w/cpp/language/operator_precedence
// The commented-out values are ones we don't currently implement.

// clang-format off
constexpr int kPrecedenceComma = 10;                  // ,  (lowest precedence)
constexpr int kPrecedenceAssignment = 20;             // = += -= *= -= /= %= <<= >>= &= ^= |=
constexpr int kPrecedenceLogicalOr = 30;              // ||
constexpr int kPrecedenceLogicalAnd = 40;             // &&
constexpr int kPrecedenceBitwiseOr = 50;              // |
constexpr int kPrecedenceBitwiseXor = 60;             // ^
constexpr int kPrecedenceBitwiseAnd = 70;             // &
constexpr int kPrecedenceEquality = 80;               // == !=
constexpr int kPrecedenceComparison = 90;             // < <= > >=
constexpr int kPrecedenceThreeWayComparison = 100;    // <=>
constexpr int kPrecedenceShift = 110;                 // << >>
constexpr int kPrecedenceAddition = 120;              // + -
constexpr int kPrecedenceMultiplication = 130;        // * / %
//constexpr int kPrecedencePointerToMember = 140;     // .* ->*
constexpr int kPrecedenceRustCast = 140;              // foo as Bar
constexpr int kPrecedenceUnary = 150;                 // ++ -- +a -a ! ~ *a &a
constexpr int kPrecedenceCallAccess = 160;            // () . -> []
//constexpr int kPrecedenceScope = 170;               // ::  (Highest precedence)
// clang-format on

// Returns true if two tokens are adjacent (have no space between them).
bool IsAdjacent(const ExprToken& a, const ExprToken& b) {
  return a.byte_offset() + a.value().size() == b.byte_offset();
}

}  // namespace

struct ExprParser::DispatchInfo {
  PrefixFunc prefix = nullptr;
  InfixFunc infix = nullptr;
  int precedence = 0;  // Only needed when |infix| is set.
};

// The table is more clear without line wrapping.
// clang-format off
ExprParser::DispatchInfo ExprParser::kDispatchInfo[] = {
    // Prefix handler              Infix handler                 Precedence for infix
    {nullptr,                      nullptr,                      -1},                             // kInvalid
    {&ExprParser::NamePrefix,      nullptr,                      -1},                             // kName
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                             // kInteger
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceAssignment},          // kEquals
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceEquality},            // kEquality
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceEquality},            // kInequality
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceComparison},          // kLessEqual
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceComparison},          // kGreaterEqual
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceThreeWayComparison},  // kSpaceship
    {nullptr,                      &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},          // kDot
    {nullptr,                      nullptr,                      -1},                             // kComma
    {nullptr,                      nullptr,                      -1},                             // kSemicolon
    {&ExprParser::StarPrefix,      &ExprParser::BinaryOpInfix,   kPrecedenceMultiplication},      // kStar
    {&ExprParser::AmpersandPrefix, &ExprParser::BinaryOpInfix,   kPrecedenceBitwiseAnd},          // kAmpersand
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceLogicalAnd},          // kDoubleAnd
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceBitwiseOr},           // kBitwiseOr
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceLogicalOr},           // kLogicalOr
    {nullptr,                      &ExprParser::DotOrArrowInfix, kPrecedenceCallAccess},          // kArrow
    {nullptr,                      &ExprParser::LeftSquareInfix, kPrecedenceCallAccess},          // kLeftSquare
    {nullptr,                      nullptr,                      -1},                             // kRightSquare
    {&ExprParser::LeftParenPrefix, &ExprParser::LeftParenInfix,  kPrecedenceCallAccess},          // kLeftParen
    {nullptr,                      nullptr,                      -1},                             // kRightParen
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceComparison},          // kLess
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceComparison},          // kGreater
    {&ExprParser::UnaryPrefix,     &ExprParser::BinaryOpInfix,   kPrecedenceAddition},            // kMinus
    {&ExprParser::UnaryPrefix,     nullptr,                      kPrecedenceUnary},               // kBang
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceAddition},            // kPlus
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceMultiplication},      // kSlash
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceBitwiseXor},          // kCaret
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceMultiplication},      // kPercent
    {&ExprParser::NamePrefix,      nullptr,                      -1},                             // kColonColon
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceShift},               // kShiftLeft
    {nullptr,                      &ExprParser::BinaryOpInfix,   kPrecedenceShift},               // kShiftRight
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                             // kTrue
    {&ExprParser::LiteralPrefix,   nullptr,                      -1},                             // kFalse
    {&ExprParser::NamePrefix,      nullptr,                      -1},                             // kConst
    {nullptr,                      nullptr,                      -1},                             // kMut
    {&ExprParser::NamePrefix,      nullptr,                      -1},                             // kVolatile
    {&ExprParser::NamePrefix,      nullptr,                      -1},                             // kRestrict
    {&ExprParser::CastPrefix,      nullptr,                      -1},                             // kReinterpretCast
    {&ExprParser::CastPrefix,      nullptr,                      -1},                             // kStaticCast
    {&ExprParser::SizeofPrefix,    nullptr,                      -1},                             // kSizeof
    {nullptr,                      &ExprParser::RustCastInfix,   kPrecedenceRustCast},            // kAs
};
// clang-format on

// static
const ExprToken ExprParser::kInvalidToken;

ExprParser::ExprParser(std::vector<ExprToken> tokens, ExprLanguage lang,
                       NameLookupCallback name_lookup)
    : language_(lang), name_lookup_callback_(std::move(name_lookup)), tokens_(std::move(tokens)) {
  static_assert(arraysize(ExprParser::kDispatchInfo) == static_cast<int>(ExprTokenType::kNumTypes),
                "kDispatchInfo needs updating to match ExprTokenType");
}

fxl::RefPtr<ExprNode> ExprParser::Parse() {
  auto result = ParseExpression(0);

  // That should have consumed everything, as we don't support multiple expressions being next to
  // each other (probably the user forgot an operator and wrote something like "foo 5"
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

// static
Err ExprParser::ParseIdentifier(const std::string& input, Identifier* output) {
  ParsedIdentifier parsed;
  Err err = ParseIdentifier(input, &parsed);
  if (err.has_error())
    return err;
  *output = ToIdentifier(parsed);
  return Err();
}

// static
Err ExprParser::ParseIdentifier(const std::string& input, ParsedIdentifier* output) {
  ExprTokenizer tokenizer(input);
  if (!tokenizer.Tokenize())
    return tokenizer.err();

  ExprParser parser(tokenizer.TakeTokens(), tokenizer.language());
  auto root = parser.Parse();
  if (!root)
    return parser.err();

  auto identifier_node = root->AsIdentifier();
  if (!identifier_node)
    return Err("Input did not parse as an identifier.");

  *output = const_cast<IdentifierExprNode*>(identifier_node)->TakeIdentifier();
  return Err();
}

fxl::RefPtr<ExprNode> ExprParser::ParseExpression(int precedence) {
  if (at_end())
    return nullptr;

  // Here we need ">>" to be treated as a shift operation. When that appears as part of a name and
  // should be separated, the identifier parser will handle it separately.
  ExprToken token = ConsumeWithShiftTokenConversion();
  PrefixFunc prefix = DispatchForToken(token).prefix;

  if (!prefix) {
    SetError(token, fxl::StringPrintf("Unexpected token '%s'.", token.value().c_str()));
    return nullptr;
  }

  fxl::RefPtr<ExprNode> left = (this->*prefix)(token);
  if (has_error())
    return left;

  while (!at_end() && precedence < CurPrecedenceWithShiftTokenConversion()) {
    const ExprToken& next_token = ConsumeWithShiftTokenConversion();
    InfixFunc infix = DispatchForToken(next_token).infix;
    if (!infix) {
      SetError(next_token, fxl::StringPrintf("Unexpected token '%s'.", next_token.value().c_str()));
      return nullptr;
    }
    left = (this->*infix)(std::move(left), next_token);
    if (has_error())
      return nullptr;
  }

  return left;
}

ExprParser::ParseNameResult ExprParser::ParseName(bool expand_types) {
  // Grammar we support. Note "identifier" in this context is a single token of type "name" (more
  // like how the C++ spec uses it), while our Identifier class represents a whole name with scopes
  // and templates.
  //
  //   name := type-name | other-identifier
  //
  //   type-name := [ scope-name "::" ] identifier [ "<" template-list ">" ]
  //
  //   other-identifier := [ <scope-name> "::" ] <identifier>
  //
  //   scope-name := ( namespace-name | type-name )
  //
  // The thing that differentiates type names, namespace names, and other identifiers is the symbol
  // lookup function rather than something in the grammar.
  //
  // The thing this doesn't handle is templatized functions, for example:
  //   auto foo = &MyClass::MyFunc<int>;
  // To handle this we will need the type lookup function to be able to tell us "MyClass::MyFunc" is
  // a thing that has a template so we know to parse the following "<" as part of the name and not
  // as a comparison. Note that when we need to parse function names, there is special handling
  // required for operators.

  // The mode of the state machine.
  enum Mode {
    kBegin,       // Initial state with no previous context.
    kColonColon,  // Just saw a "::", expecting a name next.
    kType,        // Identifier is a type.
    kTemplate,    // Identifier is a template, expecting "<" next.
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
      case ExprTokenType::kColonColon: {
        // "::" can only follow nothing, a namespace or type name.
        if (mode != kBegin && mode != kNamespace && mode != kType && mode != kAnything) {
          SetError(token,
                   "Could not identify thing to the left of '::' as a type or "
                   "namespace.");
          return ParseNameResult();
        }

        mode = kColonColon;
        if (result.ident.empty())  // Globally qualified (starts with "::").
          result.ident = ParsedIdentifier(IdentifierQualification::kGlobal);
        result.type = nullptr;  // No longer a type.
        break;
      }

      case ExprTokenType::kLess: {
        // "<" can only come after a template name.
        if (mode == kNamespace || mode == kType) {
          // Generate a nicer error for these cases.
          SetError(token, "Template parameters not valid on this object type.");
          return ParseNameResult();
        }
        if (mode != kTemplate && mode != kAnything) {
          // "<" after anything but a template means the end of the name. In "anything" mode we
          // assume "<" means a template since this is used to parse random identifiers and function
          // names.
          return result;
        }
        if (result.ident.components().back().has_template()) {
          // Got a "<" after a template parameter list was already defined (this will happen in
          // "anything" mode since we don't know what it is for sure). That means this is a
          // comparison operator which will be handled by the outer parser.
          return result;
        }

        prev_token = &Consume();  // Eat the "<".

        // Extract the contents of the template.
        std::vector<std::string> list = ParseTemplateList(ExprTokenType::kGreater);
        if (has_error())
          return ParseNameResult();

        // Ending ">".
        Consume(ExprTokenType::kGreater, "Expected '>' to match.");
        if (has_error())
          return ParseNameResult();

        // Construct a replacement for the last component of the identifier with the template
        // arguments added.
        ParsedIdentifierComponent& back = result.ident.components().back();
        back = ParsedIdentifierComponent(back.name(), std::move(list));

        // The thing we just made is either a type or a name, look it up.
        if (name_lookup_callback_) {
          FoundName lookup =
              name_lookup_callback_(result.ident, FindNameOptions(FindNameOptions::kAllKinds));
          switch (lookup.kind()) {
            case FoundName::kType:
              mode = kType;
              result.type = std::move(lookup.type());
              break;
            case FoundName::kNamespace:
            case FoundName::kTemplate:
              // The lookup shouldn't tell us a template name or namespace for something that has
              // template parameters.
              FXL_NOTREACHED();
              // Fall through to "other" case for fallback.
            case FoundName::kVariable:
            case FoundName::kMemberVariable:
            case FoundName::kFunction:
            case FoundName::kNone:
              mode = kOtherName;
              break;
          }
        } else {
          mode = kAnything;
        }
        continue;  // Don't Consume() since we already ate the token.
      }

      case ExprTokenType::kName: {
        // Names can only follow nothing or "::".
        if (mode == kType) {
          // Normally in C++ a name can follow a type, so make a special error for this case.
          SetError(token, "This looks like a declaration which is not supported.");
          return ParseNameResult();
        } else if (mode == kBegin) {
          // Found an identifier name with nothing before it.
          result.ident = ParsedIdentifier(token.value());
        } else if (mode == kColonColon) {
          result.ident.AppendComponent(ParsedIdentifierComponent(cur_token().value()));
        } else {
          // Anything else like "std::vector foo" or "foo bar".
          SetError(token, "Unexpected identifier, did you forget an operator?");
          return ParseNameResult();
        }

        // Decode what adding the name just generated.
        if (name_lookup_callback_) {
          FoundName lookup =
              name_lookup_callback_(result.ident, FindNameOptions(FindNameOptions::kAllKinds));
          switch (lookup.kind()) {
            case FoundName::kNamespace:
              mode = kNamespace;
              break;
            case FoundName::kTemplate:
              mode = kTemplate;
              break;
            case FoundName::kType:
              mode = kType;
              result.type = std::move(lookup.type());
              break;
            case FoundName::kVariable:
            case FoundName::kMemberVariable:
            case FoundName::kFunction:
            case FoundName::kNone:
              mode = kOtherName;
              break;
          }
        } else {
          mode = kAnything;
        }
        break;
      }

      default: {
        // Any other token type means we're done. The outer parser will figure out what it means.
        if (expand_types && result.type && language_ != ExprLanguage::kRust) {
          // When we found a type, add on any trailing modifiers like "*". Rust doesn't have
          // trailing modifiers, so skip this for Rust.
          result.type = ParseType(std::move(result.type));
        }
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

fxl::RefPtr<Type> ExprParser::ParseType(fxl::RefPtr<Type> optional_base) {
  // For C++, the thing we want to parse is:
  //
  //   cv-qualifier := [ "const" ] [ "volatile" ] [ "restrict" ]
  //
  //   ptr-operator := ( "*" | "&" | "&&" ) cv-qualifier
  //
  //   type-id := cv-qualifier type-name cv-qualifier [ ptr-operator ] *
  //
  // Our logic is much more permissive than C++. This is both because it makes the code simpler, and
  // because certain constructs may be used by other languages. For example, this allows references
  // to references and "int & const" while C++ says you can't apply const to the reference itself
  // (it permits only "const int&" or "int const &" which are the same). It also allows "restrict"
  // to be used in invalid places.
  //
  // For Rust, the thing we want to parse is:
  //
  //   qualifier := "&" | "*" | "mut"
  //
  //   type-id = qualifier* (type-name | array-type | tuple-type)
  //
  //   array-type = "[" type ";" integer "]"
  //
  //   tuple-type = "(" type ("," type)* ")"
  //
  // This is simpler than the C++ case. Rust has some restrictions on how qualifiers appear, and
  // again we are more permissive, mostly because Rust's mutability constraints aren't important
  // to the debugger case, so we can afford to be less particular. References and pointers are all
  // the same in the compiled code, so we interchange them freely.

  fxl::RefPtr<Type> type;
  std::vector<DwarfTag> type_qual;
  size_t pointer_levels = 0;
  if (optional_base) {
    FXL_DCHECK(language_ != ExprLanguage::kRust);
    // Type name already known, start parsing after it.
    type = std::move(optional_base);
  } else {
    if (language_ == ExprLanguage::kRust) {
      while (!at_end()) {
        const ExprToken& token = cur_token();
        if (token.type() == ExprTokenType::kStar) {
          pointer_levels++;
        } else if (token.type() == ExprTokenType::kAmpersand) {
          pointer_levels++;
        } else if (token.type() == ExprTokenType::kDoubleAnd) {
          pointer_levels += 2;
        } else if (token.type() == ExprTokenType::kMut) {
          // Ignore mut.
        } else {
          // Done with the ptr-operators.
          break;
        }
        Consume();  // Eat the operator token.
      }
    } else {
      // Read "const", etc. that comes before the type name.
      ConsumeCVQualifier(&type_qual);
      if (has_error())
        return nullptr;
    }

    // Read the type name itself.
    if (at_end()) {
      SetError(ExprToken(), "Expected type name before end of input.");
      return nullptr;
    }
    const ExprToken& first_name_token = cur_token();  // For error blame below.

    if (language_ == ExprLanguage::kRust &&
        (first_name_token.type() == ExprTokenType::kLeftSquare ||
         first_name_token.type() == ExprTokenType::kLeftParen)) {
      if (first_name_token.type() == ExprTokenType::kLeftParen) {
        Consume();

        std::vector<fxl::RefPtr<Type>> members;

        bool expect_type = true;
        while (!at_end() && cur_token().type() != ExprTokenType::kRightParen && expect_type) {
          auto next = ParseType(nullptr);
          if (!next) {
            return nullptr;
          }

          members.push_back(std::move(next));

          expect_type = !at_end() && cur_token().type() == ExprTokenType::kComma;
          if (expect_type) {
            Consume();
          }
        }

        if (at_end()) {
          SetError(ExprToken(), "Expected ')' or ',' before end of input.");
          return nullptr;
        } else if (cur_token().type() != ExprTokenType::kRightParen) {
          SetError(cur_token(), "Expected ')' or ','.");
          return nullptr;
        } else {
          Consume();
        }

        if (members.size() == 1 && !expect_type) {
          // This was just a type in parentheses. Rust appears to handle this in this way so we will
          // too.
          return std::move(members[0]);
        }

        std::string name = "(";

        for (const auto& type : members) {
          if (name.size() > 1) {
            name += ", ";
          }

          name += type->GetAssignedName();
        }

        if (members.size() == 1) {
          name += ",";
        }

        name += ")";

        return MakeRustTuple(name, members);
      } else {
        type = ParseRustArrayType();

        if (!type) {
          return nullptr;
        }
      }
    } else {
      ParseNameResult parse_result = ParseName(false);
      if (has_error())
        return nullptr;
      if (!parse_result.type) {
        SetError(first_name_token,
                 fxl::StringPrintf("Expected a type name but could not find a type named '%s'.",
                                   parse_result.ident.GetFullName().c_str()));
        return nullptr;
      }
      type = std::move(parse_result.type);
    }

    for (size_t i = 0; i < pointer_levels; i++) {
      type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, std::move(type));
    }
  }

  if (language_ == ExprLanguage::kRust) {
    return type;
  }

  // Read "const" etc. that comes after the type name. These apply the same as the ones that come
  // before it so get appended and can't duplicate them.
  ConsumeCVQualifier(&type_qual);
  if (has_error())
    return nullptr;
  type = ApplyQualifiers(std::move(type), type_qual);

  // Parse the ptr-operators that can be present after the type.
  while (!at_end()) {
    // Read the operator.
    const ExprToken& token = cur_token();
    if (token.type() == ExprTokenType::kStar) {
      type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, std::move(type));
    } else if (token.type() == ExprTokenType::kAmpersand) {
      type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, std::move(type));
    } else if (token.type() == ExprTokenType::kDoubleAnd) {
      type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kRvalueReferenceType, std::move(type));
    } else {
      // Done with the ptr-operators.
      break;
    }
    Consume();  // Eat the operator token.

    // Apply any const-volatile-restrict to the operator.
    std::vector<DwarfTag> qual;
    ConsumeCVQualifier(&qual);
    if (has_error())
      return nullptr;
    type = ApplyQualifiers(std::move(type), qual);
  }

  return type;
}

fxl::RefPtr<Type> ExprParser::ParseRustArrayType() {
  FXL_DCHECK(!at_end() && cur_token().type() == ExprTokenType::kLeftSquare);
  Consume();

  auto element = ParseType(nullptr);
  if (!element) {
    return nullptr;
  }

  std::optional<size_t> count = std::nullopt;

  if (!at_end() && cur_token().type() == ExprTokenType::kSemicolon) {
    Consume();

    if (at_end()) {
      SetError(ExprToken(), "Expected element count before end of input.");
      return nullptr;
    }

    if (cur_token().type() != ExprTokenType::kInteger) {
      SetError(cur_token(), "Expected element count.");
      return nullptr;
    }

    auto got = StringToNumber(cur_token().value());

    if (got.has_error()) {
      // Should this even be possible?
      SetError(cur_token(), "Could not parse integer: " + got.err().msg());
      return nullptr;
    }

    int64_t value;

    auto promote_err = got.value().PromoteTo64(&value);
    if (promote_err.has_error()) {
      SetError(cur_token(), "Invalid array size: " + promote_err.msg());
      return nullptr;
    }

    if (value < 0) {
      SetError(cur_token(), "Negative array size.");
      return nullptr;
    }

    Consume();

    count = static_cast<size_t>(value);
  }

  if (at_end()) {
    SetError(ExprToken(), "Expected ']' before end of input.");
    return nullptr;
  }

  if (cur_token().type() != ExprTokenType::kRightSquare) {
    SetError(cur_token(), "Expected ']'.");
    return nullptr;
  }

  Consume();

  return fxl::MakeRefCounted<ArrayType>(std::move(element), std::move(count));
}

// A list is any sequence of comma-separated types. We don't parse the types (this is hard) but
// instead skip over them.
std::vector<std::string> ExprParser::ParseTemplateList(ExprTokenType stop_before) {
  std::vector<std::string> result;

  bool first_time = true;
  while (!at_end() && !LookAhead(stop_before)) {
    if (first_time) {
      first_time = false;
    } else {
      // Expect comma to separate items.
      if (LookAhead(ExprTokenType::kComma)) {
        Consume();
      } else {
        SetError(cur_token(), "Expected ',' separating expressions.");
        return {};
      }
    }

    TemplateTypeResult type_result = ExtractTemplateType(tokens_, cur_);
    if (!type_result.success) {
      SetError(tokens_[type_result.unmatched_error_token],
               fxl::StringPrintf("Unmatched '%s'.",
                                 tokens_[type_result.unmatched_error_token].value().c_str()));
      return {};
    } else if (cur_ == type_result.end_token) {
      if (!at_end()) {
        SetError(cur_token(), "Expected template parameter.");
      } else {
        SetError(ExprToken(), "Expected template parameter.");
      }
      return {};
    }
    cur_ = type_result.end_token;
    result.push_back(std::move(type_result.canonical_name));
  }
  return result;
}

// This function is called in contexts where we expect a comma-separated list.  Currently these are
// all known in advance so this simple manual parsing will do. A more general approach would
// implement a comma infix which constructs a new type of ExprNode.
std::vector<fxl::RefPtr<ExprNode>> ExprParser::ParseExpressionList(ExprTokenType stop_before) {
  std::vector<fxl::RefPtr<ExprNode>> result;

  bool first_time = true;
  while (!at_end() && !LookAhead(stop_before)) {
    if (first_time) {
      first_time = false;
    } else {
      // Expect comma to separate items.
      if (LookAhead(ExprTokenType::kComma)) {
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
  const DispatchInfo& dispatch = DispatchForToken(token);
  fxl::RefPtr<ExprNode> right = ParseExpression(dispatch.precedence);
  if (!has_error() && !right) {
    SetError(token, fxl::StringPrintf("Expected expression after '%s'.", token.value().c_str()));
  }
  if (has_error())
    return nullptr;

  return fxl::MakeRefCounted<BinaryOpExprNode>(std::move(left), token, std::move(right));
}

fxl::RefPtr<ExprNode> ExprParser::DotOrArrowInfix(fxl::RefPtr<ExprNode> left,
                                                  const ExprToken& token) {
  // These are left-associative so use the same precedence as the token.
  fxl::RefPtr<ExprNode> right = ParseExpression(kPrecedenceCallAccess);

  auto literal = right ? right->AsLiteral() : nullptr;

  // Rust supports tuple structs, which can be addressed like "foo.0"
  if (language_ == ExprLanguage::kRust && literal && token.type() == ExprTokenType::kDot) {
    auto literal_token = literal->token();

    if (literal_token.type() == ExprTokenType::kInteger) {
      auto value = literal_token.value();

      if (value.find_first_not_of("0123456789") == std::string::npos &&
          (value.length() <= 1 || value[0] != '0')) {
        right = fxl::MakeRefCounted<IdentifierExprNode>(value);
      }
    }
  }

  if (!right || !right->AsIdentifier()) {
    SetError(token, fxl::StringPrintf("Expected identifier for right-hand-side of \"%s\".",
                                      token.value().c_str()));
    return nullptr;
  }

  // Use the name from the right-hand-side identifier, we don't need a full expression for that. If
  // we add function calls it will be necessary.
  return fxl::MakeRefCounted<MemberAccessExprNode>(std::move(left), token,
                                                   right->AsIdentifier()->ident());
}

fxl::RefPtr<ExprNode> ExprParser::LeftParenPrefix(const ExprToken& token) {
  // "(" as a prefix is a grouping or cast: "a + (b + c)" or "(Foo)bar" where it doesn't modify the
  // thing on the left. Evaluate the thing inside the () and return it.
  //
  // Currently there's no infix version of "(" which would be something like a function call.
  auto expr = ParseExpression(0);
  if (!has_error() && !expr)
    SetError(token, "Expected expression inside '('.");
  if (!has_error())
    Consume(ExprTokenType::kRightParen, "Expected ')' to match.", token);
  if (has_error())
    return nullptr;

  if (const TypeExprNode* type_expr = expr->AsType()) {
    // Convert "(TypeName)..." into a cast. Note the "-1" here which converts to right-associative.
    // With variable names, () is left-associative in that "(foo)(bar)[baz]" means to execute
    // left-to-right. But when "(foo)" is a C-style cast, this means "(bar)[baz]" is a unit.
    auto cast_expr = ParseExpression(kPrecedenceCallAccess - 1);
    if (has_error())
      return nullptr;

    fxl::RefPtr<TypeExprNode> type_ref = RefPtrTo(type_expr);
    return fxl::MakeRefCounted<CastExprNode>(CastType::kC, std::move(type_ref),
                                             std::move(cast_expr));
  }

  return expr;
}

fxl::RefPtr<ExprNode> ExprParser::LeftParenInfix(fxl::RefPtr<ExprNode> left,
                                                 const ExprToken& token) {
  // "(" as an infix is a function call. In this case, the thing on the left identifies what to
  // call (either a bare identifier or an object accessor).
  if (!FunctionCallExprNode::IsValidCall(left)) {
    SetError(token, "Unexpected '('.");
    return nullptr;
  }

  // Read the function parameters.
  std::vector<fxl::RefPtr<ExprNode>> args = ParseExpressionList(ExprTokenType::kRightParen);
  if (has_error())
    return nullptr;
  Consume(ExprTokenType::kRightParen, "Expected ')' to match.", token);
  if (has_error())
    return nullptr;

  return fxl::MakeRefCounted<FunctionCallExprNode>(std::move(left), std::move(args));
}

fxl::RefPtr<ExprNode> ExprParser::LeftSquareInfix(fxl::RefPtr<ExprNode> left,
                                                  const ExprToken& token) {
  auto inner = ParseExpression(0);
  if (!has_error() && !inner)
    SetError(token, "Expected expression inside '['.");
  if (!has_error())
    Consume(ExprTokenType::kRightSquare, "Expected ']' to match.", token);
  if (has_error())
    return nullptr;
  return fxl::MakeRefCounted<ArrayAccessExprNode>(std::move(left), std::move(inner));
}

fxl::RefPtr<ExprNode> ExprParser::RustCastInfix(fxl::RefPtr<ExprNode> left,
                                                const ExprToken& token) {
  fxl::RefPtr<Type> type = ParseType(fxl::RefPtr<Type>());
  if (has_error())
    return nullptr;
  return fxl::MakeRefCounted<CastExprNode>(
      CastType::kRust, fxl::MakeRefCounted<TypeExprNode>(std::move(type)), std::move(left));
}

fxl::RefPtr<ExprNode> ExprParser::LiteralPrefix(const ExprToken& token) {
  return fxl::MakeRefCounted<LiteralExprNode>(token);
}

fxl::RefPtr<ExprNode> ExprParser::UnaryPrefix(const ExprToken& token) {
  auto inner = ParseExpression(kPrecedenceUnary);
  if (!has_error() && !inner)
    SetError(token, fxl::StringPrintf("Expected expression for '%s'.", token.value().c_str()));
  if (has_error())
    return nullptr;
  return fxl::MakeRefCounted<UnaryOpExprNode>(token, std::move(inner));
}

fxl::RefPtr<ExprNode> ExprParser::NamePrefix(const ExprToken& token) {
  // Handles names and "::" which precedes names. This could be a typename ("int", or
  // "::std::vector<int>") or a variable name ("i", "std::basic_string<char>::npos").

  // Back up so the current token is the first component of the name so we can hand-off to the
  // specialized name parser.
  FXL_DCHECK(cur_ > 0);
  cur_--;

  if (token.type() == ExprTokenType::kConst || token.type() == ExprTokenType::kVolatile ||
      token.type() == ExprTokenType::kRestrict) {
    // These start a type name, force type parsing mode.
    fxl::RefPtr<Type> type = ParseType(fxl::RefPtr<Type>());
    if (has_error())
      return nullptr;
    return fxl::MakeRefCounted<TypeExprNode>(std::move(type));
  }

  ParseNameResult result = ParseName(true);
  if (has_error())
    return nullptr;

  if (result.type)
    return fxl::MakeRefCounted<TypeExprNode>(std::move(result.type));
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

fxl::RefPtr<ExprNode> ExprParser::CastPrefix(const ExprToken& token) {
  CastType cast_type =
      token.type() == ExprTokenType::kReinterpretCast ? CastType::kReinterpret : CastType::kStatic;

  // "<" after reinterpret_cast.
  ExprToken left_angle = Consume(ExprTokenType::kLess, "Expected '< >' after cast.");
  if (has_error())
    return nullptr;

  // Type name.
  fxl::RefPtr<Type> dest_type = ParseType(fxl::RefPtr<Type>());
  if (has_error())
    return nullptr;

  // ">" after type name.
  Consume(ExprTokenType::kGreater, "Expected '>' to match.", left_angle);
  if (has_error())
    return nullptr;

  // "(" containing expression.
  ExprToken left_paren = Consume(ExprTokenType::kLeftParen, "Expected '(' for cast.");
  if (has_error())
    return nullptr;

  // Expression to be casted.
  fxl::RefPtr<ExprNode> expr = ParseExpression(0);
  if (has_error())
    return nullptr;

  // ")" at end.
  Consume(ExprTokenType::kRightParen, "Expected ')' to match.", left_paren);
  if (has_error())
    return nullptr;

  return fxl::MakeRefCounted<CastExprNode>(
      cast_type, fxl::MakeRefCounted<TypeExprNode>(std::move(dest_type)), std::move(expr));
}

fxl::RefPtr<ExprNode> ExprParser::SizeofPrefix(const ExprToken& token) {
  // "(" containing expression.
  ExprToken left_paren = Consume(ExprTokenType::kLeftParen, "Expected '(' for cast.");
  if (has_error())
    return nullptr;

  // Expression to be sized.
  fxl::RefPtr<ExprNode> expr = ParseExpression(0);
  if (has_error())
    return nullptr;

  // ")" at end.
  Consume(ExprTokenType::kRightParen, "Expected ')' to match.", left_paren);
  if (has_error())
    return nullptr;

  return fxl::MakeRefCounted<SizeofExprNode>(std::move(expr));
}

bool ExprParser::LookAhead(ExprTokenType type) const {
  if (at_end())
    return false;
  return cur_token().type() == type;
}

const ExprToken& ExprParser::Consume() {
  if (at_end())
    return kInvalidToken;
  return tokens_[cur_++];
}

const ExprToken& ExprParser::Consume(ExprTokenType type, const char* error_msg,
                                     const ExprToken& error_token) {
  FXL_DCHECK(!has_error());  // Should have error-checked before calling.
  if (at_end()) {
    SetError(error_token, std::string(error_msg) + " Hit the end of input instead.");
    return kInvalidToken;
  }

  if (cur_token().type() == type)
    return Consume();

  SetError(error_token.type() == ExprTokenType::kInvalid ? cur_token() : error_token, error_msg);
  return kInvalidToken;
}

ExprToken ExprParser::ConsumeWithShiftTokenConversion() {
  if (IsCurTokenShiftRight()) {
    // Eat both ">" operators and synthesize a shift operator.
    const ExprToken& first = Consume();
    Consume();
    return ExprToken(ExprTokenType::kShiftRight, ">>", first.byte_offset());
  }
  return Consume();
}

void ExprParser::ConsumeCVQualifier(std::vector<DwarfTag>* qual) {
  while (!at_end()) {
    const ExprToken& token = cur_token();

    DwarfTag tag = DwarfTag::kNone;
    if (token.type() == ExprTokenType::kConst) {
      tag = DwarfTag::kConstType;
    } else if (token.type() == ExprTokenType::kVolatile) {
      tag = DwarfTag::kVolatileType;
    } else if (token.type() == ExprTokenType::kRestrict) {
      tag = DwarfTag::kRestrictType;
    } else {
      // Not a qualification token, done.
      return;
    }

    // Can't have duplicates.
    if (std::find(qual->begin(), qual->end(), tag) != qual->end()) {
      SetError(token,
               fxl::StringPrintf("Duplicate '%s' type qualification.", token.value().c_str()));
      return;
    }

    qual->push_back(tag);
    Consume();
  }
}

fxl::RefPtr<Type> ExprParser::ApplyQualifiers(fxl::RefPtr<Type> input,
                                              const std::vector<DwarfTag>& qual) {
  fxl::RefPtr<Type> type = std::move(input);

  // Apply the qualifiers in reverse order so the rightmost one is applied first.
  for (auto iter = qual.rbegin(); iter != qual.rend(); ++iter) {
    type = fxl::MakeRefCounted<ModifiedType>(*iter, std::move(type));
  }
  return type;
}

void ExprParser::SetError(const ExprToken& token, std::string msg) {
  err_ = Err(std::move(msg));
  error_token_ = token;
}

bool ExprParser::IsCurTokenShiftRight() const {
  if (tokens_.size() < 2 || cur_ > tokens_.size() - 2)
    return false;  // Not enough room for two tokens.

  if (tokens_[cur_].type() != ExprTokenType::kGreater ||
      tokens_[cur_ + 1].type() != ExprTokenType::kGreater)
    return false;  // Not two ">" in a row.

  // They must also be next to each other with no space.
  return IsAdjacent(tokens_[cur_], tokens_[cur_ + 1]);
}

int ExprParser::CurPrecedenceWithShiftTokenConversion() const {
  if (IsCurTokenShiftRight())
    return kPrecedenceShift;
  return DispatchForToken(cur_token()).precedence;
}

// static
const ExprParser::DispatchInfo& ExprParser::DispatchForToken(const ExprToken& token) {
  size_t index = static_cast<size_t>(token.type());
  FXL_DCHECK(index < arraysize(kDispatchInfo));
  return kDispatchInfo[index];
}

}  // namespace zxdb
