// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/parser.h"

#include <errno.h>
#include <lib/fit/function.h>

#include "fidl/diagnostics.h"
#include "fidl/experimental_flags.h"
#include "fidl/types.h"
#include "fidl/utils.h"

namespace fidl {

// The "case" keyword is not folded into CASE_TOKEN and CASE_IDENTIFIER because
// doing so confuses clang-format.
#define CASE_TOKEN(K) Token::KindAndSubkind(K, Token::Subkind::kNone).combined()

#define CASE_IDENTIFIER(K) Token::KindAndSubkind(Token::Kind::kIdentifier, K).combined()

#define TOKEN_TYPE_CASES                         \
  case CASE_IDENTIFIER(Token::Subkind::kNone):   \
  case CASE_IDENTIFIER(Token::Subkind::kArray):  \
  case CASE_IDENTIFIER(Token::Subkind::kVector): \
  case CASE_IDENTIFIER(Token::Subkind::kString): \
  case CASE_IDENTIFIER(Token::Subkind::kRequest)

#define TOKEN_ATTR_CASES         \
  case Token::Kind::kDocComment: \
  case Token::Kind::kLeftSquare

#define TOKEN_LITERAL_CASES                      \
  case CASE_IDENTIFIER(Token::Subkind::kTrue):   \
  case CASE_IDENTIFIER(Token::Subkind::kFalse):  \
  case CASE_TOKEN(Token::Kind::kNumericLiteral): \
  case CASE_TOKEN(Token::Kind::kStringLiteral)

namespace {

enum {
  More,
  Done,
};

template <typename T, typename Fn>
void add(std::vector<std::unique_ptr<T>>* elements, Fn producer_fn) {
  fit::function<std::unique_ptr<T>()> producer(producer_fn);
  auto element = producer();
  if (element)
    elements->emplace_back(std::move(element));
}

}  // namespace

Parser::Parser(Lexer* lexer, Reporter* reporter, ExperimentalFlags experimental_flags)
    : lexer_(lexer),
      reporter_(reporter),
      experimental_flags_(experimental_flags),
      state_(State::kNormal) {
  last_token_ = Lex();
}

std::nullptr_t Parser::Fail() { return Fail(ErrUnexpectedToken); }

std::nullptr_t Parser::Fail(std::unique_ptr<Diagnostic> err) {
  assert(err && "should not report nullptr error");
  if (Ok()) {
    err->span = last_token_.span();
    reporter_->Report(std::move(err));
  }
  return nullptr;
}

template <typename... Args>
std::nullptr_t Parser::Fail(const ErrorDef<Args...>& err, const Args&... args) {
  return Fail(err, last_token_, args...);
}

template <typename... Args>
std::nullptr_t Parser::Fail(const ErrorDef<Args...>& err, Token token, const Args&... args) {
  if (Ok()) {
    reporter_->Report(err, token, args...);
  }
  return nullptr;
}

template <typename... Args>
std::nullptr_t Parser::Fail(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                            const Args&... args) {
  if (Ok()) {
    reporter_->Report(err, span, args...);
  }
  return nullptr;
}

Parser::Modifiers Parser::ParseModifiers() {
  Modifiers modifiers;
  Token token;

  // Consume tokens until we get one that isn't a modifier, treating duplicates
  // and conflicts as immediately recovered errors. For conflicts (e.g. "strict
  // flexible" or "flexible strict"), we use the earliest one.
  for (;;) {
    switch (Peek().combined()) {
      case CASE_IDENTIFIER(Token::Subkind::kStrict):
      case CASE_IDENTIFIER(Token::Subkind::kFlexible):
        token = ConsumeToken(OfKind(Token::Kind::kIdentifier)).value();
        if (modifiers.strictness) {
          if (token.subkind() == modifiers.strictness_token->subkind()) {
            Fail(ErrDuplicateModifier, token, token.kind_and_subkind());
            RecoverOneError();
          } else {
            Fail(ErrConflictingModifier, token, token.kind_and_subkind(),
                 modifiers.strictness_token->kind_and_subkind());
            RecoverOneError();
          }
        } else {
          const auto value = token.subkind() == Token::Subkind::kStrict
                                 ? types::Strictness::kStrict
                                 : types::Strictness::kFlexible;
          modifiers.strictness = value;
          modifiers.strictness_token = token;
        }
        break;
      case CASE_IDENTIFIER(Token::Subkind::kResource):
        token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kResource)).value();
        if (modifiers.resourceness) {
          Fail(ErrDuplicateModifier, token, token.kind_and_subkind());
          RecoverOneError();
        } else {
          modifiers.resourceness = types::Resourceness::kResource;
          modifiers.resourceness_token = token;
        }
        break;
      default:
        return modifiers;
    }
  }
}

std::unique_ptr<raw::Identifier> Parser::ParseIdentifier(bool is_discarded) {
  ASTScope scope(this, is_discarded);
  std::optional<Token> token = ConsumeToken(OfKind(Token::Kind::kIdentifier));
  if (!Ok() || !token)
    return Fail();
  std::string identifier(token->data());
  if (!utils::IsValidIdentifierComponent(identifier))
    return Fail(ErrInvalidIdentifier, identifier);

  return std::make_unique<raw::Identifier>(scope.GetSourceElement());
}

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseCompoundIdentifier() {
  ASTScope scope(this);
  auto first_identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  return ParseCompoundIdentifier(scope, std::move(first_identifier));
}

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseCompoundIdentifier(
    ASTScope& scope, std::unique_ptr<raw::Identifier> first_identifier) {
  std::vector<std::unique_ptr<raw::Identifier>> components;
  components.push_back(std::move(first_identifier));

  auto parse_component = [&components, this]() {
    switch (Peek().combined()) {
      default:
        return Done;

      case CASE_TOKEN(Token::Kind::kDot):
        ConsumeToken(OfKind(Token::Kind::kDot));
        if (Ok()) {
          components.emplace_back(ParseIdentifier());
        }
        return More;
    }
  };

  while (parse_component() == More) {
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::CompoundIdentifier>(scope.GetSourceElement(), std::move(components));
}

std::unique_ptr<raw::LibraryDecl> Parser::ParseLibraryDecl() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kLibrary));
  if (!Ok())
    return Fail();

  auto library_name = ParseCompoundIdentifier();
  if (!Ok())
    return Fail();

  for (const auto& component : library_name->components) {
    std::string component_data(component->start_.data());
    if (!utils::IsValidLibraryComponent(component_data)) {
      return Fail(ErrInvalidLibraryNameComponent, component->start_, component_data);
    }
  }

  return std::make_unique<raw::LibraryDecl>(scope.GetSourceElement(), std::move(attributes),
                                            std::move(library_name));
}

std::unique_ptr<raw::StringLiteral> Parser::ParseStringLiteral() {
  ASTScope scope(this);
  ConsumeToken(OfKind(Token::Kind::kStringLiteral));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::StringLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::NumericLiteral> Parser::ParseNumericLiteral() {
  ASTScope scope(this);
  ConsumeToken(OfKind(Token::Kind::kNumericLiteral));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::NumericLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::Ordinal64> Parser::ParseOrdinal64() {
  ASTScope scope(this);

  if (!MaybeConsumeToken(OfKind(Token::Kind::kNumericLiteral)))
    return Fail(ErrMissingOrdinalBeforeType);
  if (!Ok())
    return Fail();
  auto data = scope.GetSourceElement().span().data();
  std::string string_data(data.data(), data.data() + data.size());
  errno = 0;
  unsigned long long value = strtoull(string_data.data(), nullptr, 0);
  assert(errno == 0 && "unparsable number should not be lexed.");
  if (value > std::numeric_limits<uint32_t>::max())
    return Fail(ErrOrdinalOutOfBound);
  uint32_t ordinal = static_cast<uint32_t>(value);
  if (ordinal == 0u)
    return Fail(ErrOrdinalsMustStartAtOne);

  ConsumeToken(OfKind(Token::Kind::kColon));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::Ordinal64>(scope.GetSourceElement(), ordinal);
}

std::unique_ptr<raw::TrueLiteral> Parser::ParseTrueLiteral() {
  ASTScope scope(this);
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kTrue));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::TrueLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::FalseLiteral> Parser::ParseFalseLiteral() {
  ASTScope scope(this);
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kFalse));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::FalseLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::Literal> Parser::ParseLiteral() {
  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kStringLiteral):
      return ParseStringLiteral();

    case CASE_TOKEN(Token::Kind::kNumericLiteral):
      return ParseNumericLiteral();

    case CASE_IDENTIFIER(Token::Subkind::kTrue):
      return ParseTrueLiteral();

    case CASE_IDENTIFIER(Token::Subkind::kFalse):
      return ParseFalseLiteral();

    default:
      return Fail();
  }
}

std::unique_ptr<raw::AttributeArg> Parser::ParseSubsequentAttributeArg() {
  ASTScope scope(this);
  auto name = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kEqual));
  if (!Ok())
    return Fail();

  auto value = ParseConstant();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::AttributeArg>(scope.GetSourceElement(),
                                             std::string(name->span().data()), std::move(value));
}

std::unique_ptr<raw::AttributeOld> Parser::ParseAttributeOld() {
  ASTScope scope(this);
  auto name = ParseIdentifier();
  if (!Ok())
    return Fail();
  std::unique_ptr<raw::StringLiteral> value;
  if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
    value = ParseStringLiteral();
    if (!Ok())
      return Fail();
  }

  std::string str_name("");
  if (name)
    str_name = std::string(name->span().data().data(), name->span().data().size());

  return std::make_unique<raw::AttributeOld>(scope.GetSourceElement(),
                                             raw::AttributeOld::Provenance::kDefault, str_name,
                                             std::move(value));
}

std::unique_ptr<raw::AttributeNew> Parser::ParseAttributeNew() {
  ASTScope scope(this);
  ConsumeToken(OfKind(Token::Kind::kAt));
  if (!Ok())
    return Fail();

  auto name = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::vector<std::unique_ptr<raw::AttributeArg>> args;
  if (MaybeConsumeToken(OfKind(Token::Kind::kLeftParen))) {
    if (Peek().kind() == Token::Kind::kRightParen) {
      return Fail(ErrAttributeWithEmptyParens);
    }

    // There are two valid syntaxes for attribute arg lists: single arg lists contain just the
    // arg constant by itself, like so:
    //
    //  @foo("bar") // Literal constant
    //  @baz(qux)   // Identifier constant
    //
    // Conversely, multi-argument lists must name each argument, like so:
    //
    //   @foo(a="bar",b=qux)
    //
    // To resolve this ambiguity, we will speculatively parse the first token encountered as a
    // constant.  If it is followed by a close paren, we know that we are in the single-arg case,
    // and that this parsing is correct.  If is instead followed by an equal sign, we know that this
    // is the multi-arg case, and we will extract the identifier from the constant to be used as the
    // name token for the first arg in the list.
    ASTScope arg_scope(this);
    auto maybe_constant = ParseConstant();
    if (!Ok())
      return Fail();

    switch (Peek().kind()) {
      case Token::Kind::kRightParen: {
        // This attribute has a single, unnamed argument.
        args.emplace_back(std::make_unique<raw::AttributeArg>(arg_scope.GetSourceElement(),
                                                              std::move(maybe_constant)));
        ConsumeToken(OfKind(Token::Kind::kRightParen));
        if (!Ok())
          return Fail();
        break;
      }
      case Token::Kind::kComma: {
        // Common error case: multiple arguments, but the first one is not named
        return Fail(ErrAttributeArgsMustAllBeNamed);
      }
      case Token::Kind::kEqual: {
        // This attribute has multiple arguments.
        if (maybe_constant->kind != raw::Constant::Kind::kIdentifier) {
          return Fail(ErrInvalidIdentifier, std::string(maybe_constant->span().data()));
        }
        auto constant = static_cast<raw::IdentifierConstant*>(maybe_constant.get());
        if (constant->identifier->components.size() > 1) {
          return Fail(ErrInvalidIdentifier, std::string(maybe_constant->span().data()));
        }

        ConsumeToken(OfKind(Token::Kind::kEqual));
        if (!Ok())
          return Fail();

        auto arg_name = std::move(constant->identifier);
        auto value = ParseConstant();
        if (!Ok())
          return Fail();

        args.emplace_back(std::make_unique<raw::AttributeArg>(
            arg_scope.GetSourceElement(), std::string(arg_name->span().data()), std::move(value)));
        while (Peek().kind() == Token::Kind::kComma) {
          ConsumeToken(OfKind(Token::Kind::kComma));
          if (!Ok())
            return Fail();

          auto arg = ParseSubsequentAttributeArg();
          if (!Ok()) {
            const auto result = RecoverToEndOfAttributeArg();
            if (result == RecoverResult::Failure) {
              return Fail();
            }
          }
          args.emplace_back(std::move(arg));
        }
        if (!Ok())
          Fail();

        ConsumeToken(OfKind(Token::Kind::kRightParen));
        if (!Ok())
          return Fail();
        break;
      }
      default:
        return Fail();
    }
  }

  return std::make_unique<raw::AttributeNew>(scope.GetSourceElement(),
                                             std::string(name->span().data()), std::move(args));
}

std::unique_ptr<raw::AttributeListOld> Parser::ParseAttributeListOld(
    std::unique_ptr<raw::AttributeOld> doc_comment, ASTScope& scope) {
  std::vector<raw::AttributeOld> attributes;
  if (doc_comment)
    attributes.emplace_back(std::move(*doc_comment));

  ConsumeToken(OfKind(Token::Kind::kLeftSquare));
  if (!Ok())
    return Fail();
  for (;;) {
    auto attribute = ParseAttributeOld();
    if (!Ok())
      return Fail();
    attributes.emplace_back(std::move(*attribute));
    if (!MaybeConsumeToken(OfKind(Token::Kind::kComma)))
      break;
  }
  ConsumeToken(OfKind(Token::Kind::kRightSquare));
  if (!Ok())
    return Fail();

  auto attribute_list =
      std::make_unique<raw::AttributeListOld>(scope.GetSourceElement(), std::move(attributes));
  return attribute_list;
}

std::unique_ptr<raw::AttributeListNew> Parser::ParseAttributeListNew(
    std::unique_ptr<raw::AttributeNew> doc_comment, ASTScope& scope) {
  std::vector<std::unique_ptr<raw::AttributeNew>> attributes;
  if (doc_comment)
    attributes.emplace_back(std::move(doc_comment));

  for (;;) {
    auto attribute = ParseAttributeNew();
    if (!Ok()) {
      auto result = RecoverToEndOfAttributeNew();
      if (result == RecoverResult::Failure) {
        return Fail();
      }
      if (result == RecoverResult::EndOfScope) {
        break;
      }
    } else {
      attributes.emplace_back(std::move(attribute));
    }

    if (Peek().kind() != Token::Kind::kAt) {
      break;
    }
  }

  auto attribute_list =
      std::make_unique<raw::AttributeListNew>(scope.GetSourceElement(), std::move(attributes));
  return attribute_list;
}

std::unique_ptr<raw::AttributeOld> Parser::ParseDocCommentOld() {
  ASTScope scope(this);
  std::optional<Token> doc_line;
  std::optional<Token> first_doc_line;
  while (Peek().kind() == Token::Kind::kDocComment) {
    if (first_doc_line) {
      // disallow any blank lines between this doc comment and the previous one
      std::string_view trailing_whitespace = last_token_.previous_end().data();
      if (std::count(trailing_whitespace.cbegin(), trailing_whitespace.cend(), '\n') > 1)
        reporter_->Report(WarnBlankLinesWithinDocCommentBlock, previous_token_);
    }

    doc_line = ConsumeToken(OfKind(Token::Kind::kDocComment));
    if (!Ok() || !doc_line)
      return Fail();
    if (!first_doc_line) {
      first_doc_line = doc_line;
    }
  }

  auto literal = std::make_unique<raw::DocCommentLiteral>(scope.GetSourceElement());
  if (Peek().kind() == Token::Kind::kEndOfFile)
    reporter_->Report(WarnDocCommentMustBeFollowedByDeclaration, previous_token_);

  return std::make_unique<raw::AttributeOld>(scope.GetSourceElement(),
                                             raw::AttributeOld::Provenance::kDocComment, "Doc",
                                             std::move(literal));
}

std::unique_ptr<raw::AttributeNew> Parser::ParseDocCommentNew() {
  ASTScope scope(this);
  std::optional<Token> doc_line;
  std::optional<Token> first_doc_line;
  while (Peek().kind() == Token::Kind::kDocComment) {
    if (first_doc_line) {
      // disallow any blank lines between this doc comment and the previous one
      std::string_view trailing_whitespace = last_token_.previous_end().data();
      if (std::count(trailing_whitespace.cbegin(), trailing_whitespace.cend(), '\n') > 1)
        reporter_->Report(WarnBlankLinesWithinDocCommentBlock, previous_token_);
    }

    doc_line = ConsumeToken(OfKind(Token::Kind::kDocComment));
    if (!Ok() || !doc_line)
      return Fail();
    if (!first_doc_line) {
      first_doc_line = doc_line;
    }
  }

  auto literal = std::make_unique<raw::DocCommentLiteral>(scope.GetSourceElement());
  auto constant = std::make_unique<raw::LiteralConstant>(std::move(literal));
  if (Peek().kind() == Token::Kind::kEndOfFile)
    reporter_->Report(WarnDocCommentMustBeFollowedByDeclaration, previous_token_);

  std::vector<std::unique_ptr<raw::AttributeArg>> args;
  args.emplace_back(
      std::make_unique<raw::AttributeArg>(scope.GetSourceElement(), std::move(constant)));

  auto doc_comment_attr =
      raw::AttributeNew::CreateDocComment(scope.GetSourceElement(), std::move(args));
  return std::make_unique<raw::AttributeNew>(std::move(doc_comment_attr));
}

std::unique_ptr<raw::AttributeListOld> Parser::MaybeParseAttributeListOld(bool for_parameter) {
  ASTScope scope(this);
  std::unique_ptr<raw::AttributeOld> doc_comment;
  // Doc comments must appear above attributes
  if (Peek().kind() == Token::Kind::kDocComment) {
    doc_comment = ParseDocCommentOld();
  }
  if (for_parameter && doc_comment) {
    reporter_->Report(ErrDocCommentOnParameters, previous_token_);
    return Fail();
  }
  if (Peek().kind() == Token::Kind::kLeftSquare) {
    return ParseAttributeListOld(std::move(doc_comment), scope);
  }
  // no generic attributes, start the attribute list
  if (doc_comment) {
    std::vector<raw::AttributeOld> attributes;
    if (doc_comment)
      attributes.emplace_back(std::move(*doc_comment));

    return std::make_unique<raw::AttributeListOld>(scope.GetSourceElement(), std::move(attributes));
  }
  return nullptr;
}

std::unique_ptr<raw::AttributeListNew> Parser::MaybeParseAttributeListNew(bool for_parameter) {
  ASTScope scope(this);
  std::unique_ptr<raw::AttributeNew> doc_comment;
  // Doc comments must appear above attributes
  if (Peek().kind() == Token::Kind::kDocComment) {
    doc_comment = ParseDocCommentNew();
  }
  if (for_parameter && doc_comment) {
    reporter_->Report(ErrDocCommentOnParameters, previous_token_);
    return Fail();
  }
  if (Peek().kind() == Token::Kind::kAt) {
    return ParseAttributeListNew(std::move(doc_comment), scope);
  }
  // no generic attributes, start the attribute list
  if (doc_comment) {
    std::vector<std::unique_ptr<raw::AttributeNew>> attributes;
    if (doc_comment)
      attributes.emplace_back(std::move(doc_comment));

    return std::make_unique<raw::AttributeListNew>(scope.GetSourceElement(), std::move(attributes));
  }
  return nullptr;
}

raw::AttributeList Parser::MaybeParseAttributeList() {
  if (syntax_ == fidl::utils::Syntax::kNew)
    return MaybeParseAttributeListNew();
  return MaybeParseAttributeListOld();
}

std::unique_ptr<raw::Constant> Parser::ParseConstant() {
  std::unique_ptr<raw::Constant> constant;

  switch (Peek().combined()) {
  // TODO(fxbug.dev/77561): by placing this before the kIdentifier check below, we are implicitly
  //  stating that the tokens "true" and "false" will always be interpreted as their literal
  //  constants.  Consider the following example:
  //    const true string = "abc";
  //    const foo bool = false; // "false" retains its built-in literal value, so no problem
  //    const bar bool = true;  // "true" has been redefined as a string type - should this fail?
  //  We could maintain perfect purity by always treating all tokens, even "true" and "false," as
  //  identifier (rather than literal) constants, meaning that we would never be able to parse a
  //  Token::Subkind::True|False.  Since letting people overwrite the value of true and false is
  //  undesirable for usability (and sanity) reasons, we should instead modify the compiler to
  //  specifically catch `const true|false ...` cases, and show a "don't change the meaning of
  //  true and false please" error instead.
  TOKEN_LITERAL_CASES : {
    auto literal = ParseLiteral();
    if (!Ok())
      return Fail();
    constant = std::make_unique<raw::LiteralConstant>(std::move(literal));
    break;
  }

  case CASE_TOKEN(Token::Kind::kLeftParen): {
    ASTScope scope(this);
    ConsumeToken(OfKind(Token::Kind::kLeftParen));
    constant = ParseConstant();
    ConsumeToken(OfKind(Token::Kind::kRightParen));
    if (!Ok())
      return Fail();
    constant->update_span(scope.GetSourceElement());
    break;
  }

  default: {
    if (Peek().kind() == Token::Kind::kIdentifier) {
      auto identifier = ParseCompoundIdentifier();
      if (!Ok())
        return Fail();
      constant = std::make_unique<raw::IdentifierConstant>(std::move(identifier));
    } else {
      return Fail();
    }
  }
  }

  if (Peek().combined() == Token::Kind::kPipe) {
    ConsumeToken(OfKind(Token::Kind::kPipe));
    std::unique_ptr right_operand = ParseConstant();
    if (!Ok())
      return Fail();
    return std::make_unique<raw::BinaryOperatorConstant>(
        std::move(constant), std::move(right_operand), raw::BinaryOperatorConstant::Operator::kOr);
  }
  return constant;
}

std::unique_ptr<raw::AliasDeclaration> Parser::ParseAliasDeclaration(raw::AttributeList attributes,
                                                                     ASTScope& scope,
                                                                     const Modifiers& modifiers) {
  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kAlias));
  if (!Ok())
    return Fail();

  ValidateModifiers</* none */>(modifiers, decl_token.value());

  auto alias = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kEqual));
  if (!Ok())
    return Fail();

  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::AliasDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                 std::move(alias), std::move(type_ctor));
}

std::unique_ptr<raw::Using> Parser::ParseUsing(raw::AttributeList attributes, ASTScope& scope,
                                               const Modifiers& modifiers) {
  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUsing));
  if (!Ok())
    return Fail();
  auto decl_start_token = decl_token.value();

  ValidateModifiers</* none */>(modifiers, decl_start_token);

  auto using_path = ParseCompoundIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::Identifier> maybe_alias;
  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kAs))) {
    if (!Ok())
      return Fail();
    maybe_alias = ParseIdentifier();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::Using>(scope.GetSourceElement(), std::move(attributes),
                                      std::move(using_path), std::move(maybe_alias));
}

std::unique_ptr<raw::TypeConstructorOld> Parser::ParseTypeConstructorOld() {
  ASTScope scope(this);
  auto identifier = ParseCompoundIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::TypeConstructorOld> maybe_arg_type_ctor;
  std::unique_ptr<raw::Constant> handle_rights;
  std::unique_ptr<raw::Constant> maybe_size;
  std::unique_ptr<raw::Identifier> handle_subtype_identifier;
  auto nullability = types::Nullability::kNonnullable;

  if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
    if (!Ok())
      return Fail();
    maybe_arg_type_ctor = ParseTypeConstructorOld();
    if (!Ok())
      return Fail();
    ConsumeToken(OfKind(Token::Kind::kRightAngle));
    if (!Ok())
      return Fail();
  }

  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    if (!Ok())
      return Fail();
    // TODO(fxbug.dev/64629): To properly generalize handle, while supporting
    // all the features which currently exist, we will need to parse a much more
    // liberal grammar at this stage (a 'type constructor'), and defer the
    // interpretation of this data to the compilation step.
    if (identifier->components.back()->span().data() == "handle") {
      if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
        handle_subtype_identifier = ParseIdentifier();
        if (MaybeConsumeToken(OfKind(Token::Kind::kComma))) {
          handle_rights = ParseConstant();
        }
        ConsumeToken(OfKind(Token::Kind::kRightAngle));
        if (!Ok())
          return Fail();
      } else {
        handle_subtype_identifier = ParseIdentifier();
      }
    } else {
      maybe_size = ParseConstant();
    }
    if (!Ok())
      return Fail();
  }
  if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
    if (!Ok())
      return Fail();
    nullability = types::Nullability::kNullable;
  }

  return std::make_unique<raw::TypeConstructorOld>(
      scope.GetSourceElement(), std::move(identifier), std::move(maybe_arg_type_ctor),
      std::move(handle_subtype_identifier), std::move(handle_rights), std::move(maybe_size),
      nullability);
}

std::unique_ptr<raw::BitsMember> Parser::ParseBitsMember() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeListOld();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kEqual));
  if (!Ok())
    return Fail();

  auto member_value = ParseConstant();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::BitsMember>(scope.GetSourceElement(), std::move(identifier),
                                           std::move(member_value), std::move(attributes));
}

std::unique_ptr<raw::BitsDeclaration> Parser::ParseBitsDeclaration(
    std::unique_ptr<raw::AttributeListOld> attributes, ASTScope& scope,
    const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::BitsMember>> members;
  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kBits));
  if (!Ok())
    return Fail();
  auto decl_start_token = decl_token.value();

  ValidateModifiers<types::Strictness>(modifiers, decl_start_token);

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::TypeConstructorOld> maybe_type_ctor;
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    if (!Ok())
      return Fail();
    maybe_type_ctor = ParseTypeConstructorOld();
    if (!Ok())
      return Fail();
  }

  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  auto parse_member = [&members, this]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      add(&members, [&] { return ParseBitsMember(); });
      return More;
    }
  };

  auto checkpoint = reporter_->Checkpoint();
  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    Fail();

  if (!checkpoint.NoNewErrors())
    return nullptr;

  if (members.empty())
    return Fail(ErrMustHaveOneMember);

  if (modifiers.strictness != std::nullopt) {
    decl_start_token = modifiers.strictness_token.value();
  }

  return std::make_unique<raw::BitsDeclaration>(
      scope.GetSourceElement(), std::make_unique<Token>(decl_start_token), std::move(attributes),
      std::move(identifier), std::move(maybe_type_ctor), std::move(members),
      modifiers.strictness.value_or(types::Strictness::kStrict));
}

std::unique_ptr<raw::ConstDeclaration> Parser::ParseConstDeclaration(raw::AttributeList attributes,
                                                                     ASTScope& scope,
                                                                     const Modifiers& modifiers) {
  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kConst));
  if (!Ok())
    return Fail();

  ValidateModifiers</* none */>(modifiers, decl_token.value());

  // TODO(fxbug.dev/70247): remove branching
  raw::TypeConstructor type_ctor;
  std::unique_ptr<raw::Identifier> identifier;
  if (syntax_ == utils::Syntax::kNew) {
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  } else {
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
  }

  ConsumeToken(OfKind(Token::Kind::kEqual));
  if (!Ok())
    return Fail();
  auto constant = ParseConstant();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::ConstDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                 std::move(type_ctor), std::move(identifier),
                                                 std::move(constant));
}

std::unique_ptr<raw::EnumMember> Parser::ParseEnumMember() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeListOld();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kEqual));
  if (!Ok())
    return Fail();

  auto member_value = ParseConstant();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::EnumMember>(scope.GetSourceElement(), std::move(identifier),
                                           std::move(member_value), std::move(attributes));
}

std::unique_ptr<raw::EnumDeclaration> Parser::ParseEnumDeclaration(
    std::unique_ptr<raw::AttributeListOld> attributes, ASTScope& scope,
    const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::EnumMember>> members;
  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kEnum));
  if (!Ok())
    return Fail();
  auto decl_start_token = decl_token.value();

  ValidateModifiers<types::Strictness>(modifiers, decl_start_token);

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::TypeConstructorOld> maybe_type_ctor;
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    if (!Ok())
      return Fail();
    maybe_type_ctor = ParseTypeConstructorOld();
    if (!Ok())
      return Fail();
  }

  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  auto parse_member = [&members, this]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      add(&members, [&] { return ParseEnumMember(); });
      return More;
    }
  };

  auto checkpoint = reporter_->Checkpoint();
  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    Fail();

  if (!checkpoint.NoNewErrors())
    return nullptr;

  if (members.empty())
    return Fail(ErrMustHaveOneMember);

  if (modifiers.strictness != std::nullopt) {
    decl_start_token = modifiers.strictness_token.value();
  }

  return std::make_unique<raw::EnumDeclaration>(
      scope.GetSourceElement(), std::make_unique<Token>(decl_start_token), std::move(attributes),
      std::move(identifier), std::move(maybe_type_ctor), std::move(members),
      modifiers.strictness.value_or(types::Strictness::kStrict));
}

std::unique_ptr<raw::Parameter> Parser::ParseParameter() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeListOld(/*for_parameter=*/true);
  if (!Ok())
    return Fail();

  // TODO(fxbug.dev/70247): remove branching
  raw::TypeConstructor type_ctor;
  std::unique_ptr<raw::Identifier> identifier;
  if (syntax_ == utils::Syntax::kNew) {
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  } else {
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::Parameter>(scope.GetSourceElement(), std::move(type_ctor),
                                          std::move(identifier), std::move(attributes));
}

std::unique_ptr<raw::ParameterListOld> Parser::ParseParameterListOld() {
  ASTScope scope(this);
  std::vector<std::unique_ptr<raw::Parameter>> parameter_list;

  ConsumeToken(OfKind(Token::Kind::kLeftParen));
  if (!Ok())
    return Fail();

  if (Peek().kind() != Token::Kind::kRightParen) {
    auto parameter = ParseParameter();
    parameter_list.emplace_back(std::move(parameter));
    if (!Ok()) {
      const auto result = RecoverToEndOfParam();
      if (result == RecoverResult::Failure) {
        return Fail();
      }
    }
    while (Peek().kind() == Token::Kind::kComma) {
      ConsumeToken(OfKind(Token::Kind::kComma));
      if (!Ok())
        return Fail();
      parameter_list.emplace_back(ParseParameter());
      if (!Ok()) {
        const auto result = RecoverToEndOfParam();
        if (result == RecoverResult::Failure) {
          return Fail();
        }
      }
    }
  }

  ConsumeToken(OfKind(Token::Kind::kRightParen));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::ParameterListOld>(scope.GetSourceElement(),
                                                 std::move(parameter_list));
}

std::unique_ptr<raw::ParameterListNew> Parser::ParseParameterListNew() {
  ASTScope scope(this);
  std::unique_ptr<raw::TypeConstructorNew> type_ctor;

  ConsumeToken(OfKind(Token::Kind::kLeftParen));
  if (!Ok())
    return Fail();

  if (Peek().kind() != Token::Kind::kRightParen) {
    type_ctor = ParseTypeConstructorNew();
    if (!Ok()) {
      const auto result = RecoverToEndOfParamList();
      if (result == RecoverResult::Failure) {
        return Fail();
      }
    }

    if (type_ctor && type_ctor->layout_ref->kind == raw::LayoutReference::Kind::kInline) {
      const auto* layout =
          static_cast<const raw::InlineLayoutReference*>(type_ctor->layout_ref.get());
      if (layout->attributes != nullptr) {
        auto& attrs = layout->attributes->attributes;
        if (!attrs.empty() && attrs[0]->name == "doc") {
          auto& args = attrs[0]->args;
          if (!args.empty() && args[0]->value->kind == raw::Constant::Kind::kLiteral) {
            auto literal_constant = static_cast<raw::LiteralConstant*>(args[0]->value.get());
            if (literal_constant->literal->kind == raw::Literal::Kind::kDocComment) {
              Fail(ErrDocCommentOnParameters, attrs[0]->span());
              const auto result = RecoverToEndOfParamList();
              if (result == RecoverResult::Failure) {
                return Fail();
              }
            }
          }
        }
      }
    }
  }

  ConsumeToken(OfKind(Token::Kind::kRightParen));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::ParameterListNew>(scope.GetSourceElement(), std::move(type_ctor));
}

raw::ParameterList Parser::ParseParameterList() {
  if (syntax_ == fidl::utils::Syntax::kNew) {
    return ParseParameterListNew();
  }
  return ParseParameterListOld();
}

std::unique_ptr<raw::ProtocolMethod> Parser::ParseProtocolEvent(raw::AttributeList attributes,
                                                                ASTScope& scope) {
  ConsumeToken(OfKind(Token::Kind::kArrow));
  if (!Ok())
    return Fail();

  auto method_name = ParseIdentifier();
  if (!Ok())
    return Fail();

  auto parse_params = [this](raw::ParameterList* params_out) {
    if (!Ok())
      return false;
    *params_out = ParseParameterList();
    if (!Ok())
      return false;

    return true;
  };

  std::unique_ptr<raw::ParameterListOld> request;
  raw::ParameterList response;
  if (!parse_params(&response))
    return Fail();

  raw::TypeConstructor maybe_error;
  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kError))) {
    maybe_error = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  }

  assert(method_name);
  assert(raw::IsParameterListDefined(response));

  return std::make_unique<raw::ProtocolMethod>(scope.GetSourceElement(), std::move(attributes),
                                               std::move(method_name), std::move(request),
                                               std::move(response), std::move(maybe_error));
}

std::unique_ptr<raw::ProtocolMethod> Parser::ParseProtocolMethod(
    raw::AttributeList attributes, ASTScope& scope, std::unique_ptr<raw::Identifier> method_name) {
  auto parse_params = [this](raw::ParameterList* params_out) {
    *params_out = ParseParameterList();
    if (!Ok())
      return false;
    return true;
  };

  raw::ParameterList request;
  if (!parse_params(&request))
    return Fail();

  raw::ParameterList maybe_response;
  raw::TypeConstructor maybe_error;
  if (MaybeConsumeToken(OfKind(Token::Kind::kArrow))) {
    if (!Ok())
      return Fail();
    if (!parse_params(&maybe_response))
      return Fail();
    if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kError))) {
      maybe_error = ParseTypeConstructor();
      if (!Ok())
        return Fail();
    }
  }

  assert(method_name);
  assert(raw::IsParameterListDefined(request));

  return std::make_unique<raw::ProtocolMethod>(scope.GetSourceElement(), std::move(attributes),
                                               std::move(method_name), std::move(request),
                                               std::move(maybe_response), std::move(maybe_error));
}

std::unique_ptr<raw::ProtocolCompose> Parser::ParseProtocolCompose(raw::AttributeList attributes,
                                                                   ASTScope& scope) {
  auto identifier = ParseCompoundIdentifier();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::ProtocolCompose>(scope.GetSourceElement(), std::move(attributes),
                                                std::move(identifier));
}

void Parser::ParseProtocolMember(
    std::vector<std::unique_ptr<raw::ProtocolCompose>>* composed_protocols,
    std::vector<std::unique_ptr<raw::ProtocolMethod>>* methods) {
  ASTScope scope(this);
  raw::AttributeList attributes = MaybeParseAttributeList();
  if (!Ok()) {
    Fail();
    return;
  }

  switch (Peek().kind()) {
    case Token::Kind::kArrow: {
      add(methods, [&] { return ParseProtocolEvent(std::move(attributes), scope); });
      break;
    }
    case Token::Kind::kIdentifier: {
      std::unique_ptr<raw::Identifier> method_name;
      if (Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kCompose)) {
        // There are two possibilities here: we are looking at the first token in a compose
        // statement like `compose a.b;`, or we are looking at the identifier of a method that has
        // unfortunately been named `compose(...);`.  Because we want the previous_end of the
        // CompoundIdentifier to correctly point to the previous raw AST node, instead of calling
        // ParseIdentifier here, we merely consume the token for now.
        const auto compose_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kCompose));
        if (!Ok()) {
          Fail();
          break;
        }

        // If the `compose` identifier is not immediately followed by a left paren we assume that we
        // are looking at a compose clause.  Because we only we haven't built any raw AST nodes
        // since the compose clause started, the previous_end of its raw AST node will point to the
        // correct position.
        if (Peek().kind() != Token::Kind::kLeftParen) {
          add(composed_protocols,
              [&] { return ParseProtocolCompose(std::move(attributes), scope); });
          return;
        }

        // Looks like this is a `compose(...);` method after all, so coerce the composed token into
        // an Identifier source element.
        method_name = std::make_unique<raw::Identifier>(
            raw::SourceElement(compose_token.value(), compose_token.value()));
      } else {
        method_name = ParseIdentifier();
        if (!Ok()) {
          Fail();
          return;
        }
        if (Peek().kind() != Token::Kind::kLeftParen) {
          Fail(ErrUnrecognizedProtocolMember);
          return;
        }
      }

      add(methods, [&] {
        return ParseProtocolMethod(std::move(attributes), scope, std::move(method_name));
      });
      break;
    }
    default:
      Fail(ErrExpectedProtocolMember);
      return;
  }
}

std::unique_ptr<raw::ProtocolDeclaration> Parser::ParseProtocolDeclaration(
    raw::AttributeList attributes, ASTScope& scope, const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::ProtocolCompose>> composed_protocols;
  std::vector<std::unique_ptr<raw::ProtocolMethod>> methods;

  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kProtocol));
  if (!Ok())
    return Fail();

  ValidateModifiers</* none */>(modifiers, decl_token.value());

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  auto parse_member = [&composed_protocols, &methods, this]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      ParseProtocolMember(&composed_protocols, &methods);
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    Fail();

  return std::make_unique<raw::ProtocolDeclaration>(
      scope.GetSourceElement(), std::move(attributes), std::move(identifier),
      std::move(composed_protocols), std::move(methods));
}

std::unique_ptr<raw::ResourceProperty> Parser::ParseResourcePropertyDeclaration() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();

  // TODO(fxbug.dev/70247): remove branching
  raw::TypeConstructor type_ctor;
  std::unique_ptr<raw::Identifier> identifier;
  if (syntax_ == utils::Syntax::kNew) {
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  } else {
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::ResourceProperty>(scope.GetSourceElement(), std::move(type_ctor),
                                                 std::move(identifier), std::move(attributes));
}

std::unique_ptr<raw::ResourceDeclaration> Parser::ParseResourceDeclaration(
    raw::AttributeList attributes, ASTScope& scope, const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::ResourceProperty>> properties;

  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kResourceDefinition));
  if (!Ok())
    return Fail();

  ValidateModifiers</* none */>(modifiers, decl_token.value());

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  raw::TypeConstructor maybe_type_ctor;
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    // TODO(fxbug.dev/70247): remove branching
    if (syntax_ == utils::Syntax::kNew) {
      ASTScope type_identifier_scope(this);
      auto resource_type_identifier = ParseCompoundIdentifier();
      if (!Ok())
        return Fail();

      maybe_type_ctor = std::make_unique<raw::TypeConstructorNew>(
          scope.GetSourceElement(),
          std::make_unique<raw::NamedLayoutReference>(type_identifier_scope.GetSourceElement(),
                                                      std::move(resource_type_identifier)),
          /*parameters=*/nullptr,
          /*constraints=*/nullptr);
    } else {
      if (!Ok()) {
        return Fail();
      }
      maybe_type_ctor = ParseTypeConstructor();
    }
    if (!Ok())
      return Fail();
  }

  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  // Just the scaffolding of the resource here, only properties is currently accepted.
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kProperties));
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  auto parse_prop = [&properties, this]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      add(&properties, [&] { return ParseResourcePropertyDeclaration(); });
      return More;
    }
  };

  auto checkpoint = reporter_->Checkpoint();
  while (parse_prop() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    Fail();

  if (!checkpoint.NoNewErrors())
    return nullptr;

  if (properties.empty())
    return Fail(ErrMustHaveOneProperty);

  // End of properties block.
  ConsumeToken(OfKind(Token::Kind::kSemicolon));
  if (!Ok())
    return Fail();

  // End of resource.
  ConsumeToken(OfKind(Token::Kind::kRightCurly));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::ResourceDeclaration>(
      scope.GetSourceElement(), std::move(attributes), std::move(identifier),
      std::move(maybe_type_ctor), std::move(properties));
}

std::unique_ptr<raw::ServiceMember> Parser::ParseServiceMember() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();

  // TODO(fxbug.dev/70247): remove branching
  raw::TypeConstructor type_ctor;
  std::unique_ptr<raw::Identifier> identifier;
  if (syntax_ == utils::Syntax::kNew) {
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  } else {
    type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::ServiceMember>(scope.GetSourceElement(), std::move(type_ctor),
                                              std::move(identifier), std::move(attributes));
}

std::unique_ptr<raw::ServiceDeclaration> Parser::ParseServiceDeclaration(
    raw::AttributeList attributes, ASTScope& scope, const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::ServiceMember>> members;

  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kService));
  if (!Ok())
    return Fail();

  ValidateModifiers</* none */>(modifiers, decl_token.value());

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();
  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  auto parse_member = [&]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      add(&members, [&] { return ParseServiceMember(); });
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    Fail();

  return std::make_unique<raw::ServiceDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                   std::move(identifier), std::move(members));
}

std::unique_ptr<raw::StructMember> Parser::ParseStructMember() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeListOld();
  if (!Ok())
    return Fail();
  auto type_ctor = ParseTypeConstructorOld();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::Constant> maybe_default_value;
  if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
    if (!Ok())
      return Fail();
    maybe_default_value = ParseConstant();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::StructMember>(scope.GetSourceElement(), std::move(type_ctor),
                                             std::move(identifier), std::move(maybe_default_value),
                                             std::move(attributes));
}

std::unique_ptr<raw::StructDeclaration> Parser::ParseStructDeclaration(
    std::unique_ptr<raw::AttributeListOld> attributes, ASTScope& scope,
    const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::StructMember>> members;

  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kStruct));
  if (!Ok())
    return Fail();
  auto decl_start_token = decl_token.value();

  ValidateModifiers<types::Resourceness>(modifiers, decl_start_token);

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();
  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  auto parse_member = [&members, this]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      add(&members, [&] { return ParseStructMember(); });
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    return Fail();

  const auto resourceness = modifiers.resourceness.value_or(types::Resourceness::kValue);
  if (resourceness == types::Resourceness::kResource) {
    decl_start_token = modifiers.resourceness_token.value();
  }

  return std::make_unique<raw::StructDeclaration>(
      scope.GetSourceElement(), std::make_unique<Token>(decl_start_token), std::move(attributes),
      std::move(identifier), std::move(members), resourceness);
}

std::unique_ptr<raw::TableMember> Parser::ParseTableMember() {
  ASTScope scope(this);
  std::unique_ptr<raw::AttributeListOld> attributes = MaybeParseAttributeListOld();
  if (!Ok())
    return Fail();

  auto ordinal = ParseOrdinal64();
  if (!Ok())
    return Fail();

  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kReserved))) {
    if (!Ok())
      return Fail();
    if (attributes != nullptr)
      return Fail(ErrCannotAttachAttributesToReservedOrdinals);
    return std::make_unique<raw::TableMember>(scope.GetSourceElement(), std::move(ordinal));
  }

  auto type_ctor = ParseTypeConstructorOld();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::Constant> maybe_default_value;
  if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
    if (!Ok())
      return Fail();
    maybe_default_value = ParseConstant();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::TableMember>(scope.GetSourceElement(), std::move(ordinal),
                                            std::move(type_ctor), std::move(identifier),
                                            std::move(maybe_default_value), std::move(attributes));
}

std::unique_ptr<raw::TableDeclaration> Parser::ParseTableDeclaration(
    std::unique_ptr<raw::AttributeListOld> attributes, ASTScope& scope,
    const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::TableMember>> members;

  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kTable));
  if (!Ok())
    return Fail();
  auto decl_start_token = decl_token.value();

  ValidateModifiers<types::Resourceness>(modifiers, decl_start_token);

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();
  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  auto parse_member = [&members, this]() {
    switch (Peek().combined()) {
      case CASE_TOKEN(Token::Kind::kRightCurly):
        ConsumeToken(OfKind(Token::Kind::kRightCurly));
        return Done;

      case CASE_TOKEN(Token::Kind::kNumericLiteral):
      TOKEN_ATTR_CASES : {
        add(&members, [&] { return ParseTableMember(); });
        return More;
      }

      default:
        Fail(ErrExpectedOrdinalOrCloseBrace, Peek());
        return Done;
    }
  };

  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    Fail();

  const auto resourceness = modifiers.resourceness.value_or(types::Resourceness::kValue);
  if (resourceness == types::Resourceness::kResource) {
    decl_start_token = modifiers.resourceness_token.value();
  }

  return std::make_unique<raw::TableDeclaration>(
      scope.GetSourceElement(), std::make_unique<Token>(decl_start_token), std::move(attributes),
      std::move(identifier), std::move(members), types::Strictness::kFlexible, resourceness);
}

std::unique_ptr<raw::UnionMember> Parser::ParseUnionMember() {
  ASTScope scope(this);

  auto attributes = MaybeParseAttributeListOld();
  if (!Ok())
    return Fail();
  auto ordinal = ParseOrdinal64();
  if (!Ok())
    return Fail();

  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kReserved))) {
    if (!Ok())
      return Fail();
    if (attributes)
      return Fail(ErrCannotAttachAttributesToReservedOrdinals);
    return std::make_unique<raw::UnionMember>(scope.GetSourceElement(), std::move(ordinal));
  }

  auto type_ctor = ParseTypeConstructorOld();
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::Constant> maybe_default_value;
  if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
    if (!Ok())
      return Fail();
    maybe_default_value = ParseConstant();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::UnionMember>(scope.GetSourceElement(), std::move(ordinal),
                                            std::move(type_ctor), std::move(identifier),
                                            std::move(maybe_default_value), std::move(attributes));
}

std::unique_ptr<raw::UnionDeclaration> Parser::ParseUnionDeclaration(
    std::unique_ptr<raw::AttributeListOld> attributes, ASTScope& scope,
    const Modifiers& modifiers) {
  std::vector<std::unique_ptr<raw::UnionMember>> members;

  const auto decl_token = ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUnion));
  if (!Ok())
    return Fail();
  auto decl_start_token = decl_token.value();

  ValidateModifiers<types::Strictness, types::Resourceness>(modifiers, decl_start_token);

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  bool contains_non_reserved_member = false;
  auto parse_member = [&]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      auto member = ParseUnionMember();
      if (member) {
        members.emplace_back(std::move(member));
        if (members.back() && members.back()->maybe_used)
          contains_non_reserved_member = true;
      }
      return More;
    }
  };

  auto checkpoint = reporter_->Checkpoint();
  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    return Fail();

  if (!checkpoint.NoNewErrors())
    return nullptr;

  if (!contains_non_reserved_member)
    return Fail(ErrMustHaveNonReservedMember);

  const auto resourceness = modifiers.resourceness.value_or(types::Resourceness::kValue);
  if (modifiers.resourceness_token && modifiers.strictness_token) {
    // If both modifiers are present, the decl_start_token is whichever occurred
    // earlier in the file.
    if (modifiers.resourceness_token->span() < modifiers.strictness_token->span()) {
      decl_start_token = modifiers.resourceness_token.value();
    } else {
      decl_start_token = modifiers.strictness_token.value();
    }
  } else if (modifiers.resourceness_token) {
    decl_start_token = modifiers.resourceness_token.value();
  } else if (modifiers.strictness_token) {
    decl_start_token = modifiers.strictness_token.value();
  }

  return std::make_unique<raw::UnionDeclaration>(
      scope.GetSourceElement(), std::make_unique<Token>(decl_start_token), std::move(attributes),
      std::move(identifier), std::move(members),
      modifiers.strictness.value_or(types::Strictness::kStrict),
      modifiers.strictness != std::nullopt, resourceness);
}

std::unique_ptr<raw::File> Parser::ParseFile() {
  ASTScope scope(this);

  const bool old_only = experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kOldSyntaxOnly);
  const bool either = experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kAllowNewSyntax);
  const bool new_only = experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kNewSyntaxOnly);
  std::vector<const bool> token_flags = {old_only, either, new_only};
  assert(std::count(token_flags.cbegin(), token_flags.cend(), true) <= 1 &&
         "at most one of the token flags can be set");

  const bool has_syntax_token =
      MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kDeprecatedSyntax)).has_value();
  if (has_syntax_token)
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));

  if (old_only) {
    // accept token; always old syntax
    syntax_ = utils::Syntax::kOld;
  } else if (either) {
    // determine syntax based on token
    syntax_ = has_syntax_token ? utils::Syntax::kOld : utils::Syntax::kNew;
  } else if (new_only) {
    // no token allowed; always new syntax
    if (has_syntax_token)
      Fail(ErrRemoveSyntaxVersion);
    syntax_ = utils::Syntax::kNew;
  } else {
    // no token allowed; always old syntax
    if (has_syntax_token) {
      Fail(ErrRemoveSyntaxVersion);
    }
    syntax_ = utils::Syntax::kOld;
  }

  auto library_decl = ParseLibraryDecl();
  if (!Ok())
    return Fail();
  ConsumeToken(OfKind(Token::Kind::kSemicolon));
  if (!Ok())
    return Fail();

  if (syntax_ == utils::Syntax::kNew)
    return ParseFileNewSyntax(scope, std::move(library_decl));

  bool done_with_library_imports = false;
  std::vector<std::unique_ptr<raw::AliasDeclaration>> alias_list;
  std::vector<std::unique_ptr<raw::Using>> using_list;
  std::vector<std::unique_ptr<raw::BitsDeclaration>> bits_declaration_list;
  std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
  std::vector<std::unique_ptr<raw::EnumDeclaration>> enum_declaration_list;
  std::vector<std::unique_ptr<raw::ProtocolDeclaration>> protocol_declaration_list;
  std::vector<std::unique_ptr<raw::ResourceDeclaration>> resource_declaration_list;
  std::vector<std::unique_ptr<raw::ServiceDeclaration>> service_declaration_list;
  std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;
  std::vector<std::unique_ptr<raw::TableDeclaration>> table_declaration_list;
  std::vector<std::unique_ptr<raw::UnionDeclaration>> union_declaration_list;
  std::vector<std::unique_ptr<raw::TypeDecl>> type_decls;
  auto parse_declaration = [&alias_list, &bits_declaration_list, &const_declaration_list,
                            &enum_declaration_list, &protocol_declaration_list,
                            &resource_declaration_list, &service_declaration_list,
                            &struct_declaration_list, &done_with_library_imports, &using_list,
                            &table_declaration_list, &union_declaration_list, this]() {
    ASTScope scope(this);
    std::unique_ptr<raw::AttributeListOld> attributes = MaybeParseAttributeListOld();
    if (!Ok())
      return More;

    const auto modifiers = ParseModifiers();

    switch (Peek().combined()) {
      default:
        Fail(ErrExpectedDeclaration, last_token_.data());
        return More;

      case CASE_TOKEN(Token::Kind::kEndOfFile):
        return Done;

      case CASE_IDENTIFIER(Token::Subkind::kDeprecatedSyntax): {
        if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kAllowNewSyntax)) {
          Fail(ErrMisplacedSyntaxVersion);
        } else {
          Fail(ErrRemoveSyntaxVersion);
        }
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kAlias): {
        done_with_library_imports = true;
        add(&alias_list,
            [&] { return ParseAliasDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kBits): {
        done_with_library_imports = true;
        add(&bits_declaration_list,
            [&] { return ParseBitsDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kConst): {
        done_with_library_imports = true;
        add(&const_declaration_list,
            [&] { return ParseConstDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kEnum): {
        done_with_library_imports = true;
        add(&enum_declaration_list,
            [&] { return ParseEnumDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kProtocol): {
        done_with_library_imports = true;
        add(&protocol_declaration_list,
            [&] { return ParseProtocolDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kResourceDefinition): {
        done_with_library_imports = true;
        add(&resource_declaration_list,
            [&] { return ParseResourceDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kService): {
        done_with_library_imports = true;
        add(&service_declaration_list,
            [&] { return ParseServiceDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kStruct): {
        done_with_library_imports = true;
        add(&struct_declaration_list,
            [&] { return ParseStructDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kTable): {
        done_with_library_imports = true;
        add(&table_declaration_list,
            [&] { return ParseTableDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kUsing): {
        auto using_decl = ParseUsing(std::move(attributes), scope, modifiers);
        if (using_decl == nullptr) {
          // Failed to parse using declaration.
          return Done;
        }
        if (done_with_library_imports) {
          reporter_->Report(ErrLibraryImportsMustBeGroupedAtTopOfFile, using_decl->span());
        }
        using_list.emplace_back(std::move(using_decl));
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kUnion): {
        done_with_library_imports = true;
        add(&union_declaration_list,
            [&] { return ParseUnionDeclaration(std::move(attributes), scope, modifiers); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kXUnion):
        switch (modifiers.strictness.value_or(types::Strictness::kFlexible)) {
          case types::Strictness::kFlexible:
            Fail(ErrXunionDeprecated);
            return More;
          case types::Strictness::kStrict:
            Fail(ErrStrictXunionDeprecated);
            return More;
        }
    }
  };

  while (parse_declaration() == More) {
    if (!Ok()) {
      // If this returns RecoverResult::Continue, we have consumed up to a '}'
      // and expect a ';' to follow.
      auto result = RecoverToEndOfDecl();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        break;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }

  std::optional<Token> end = ConsumeToken(OfKind(Token::Kind::kEndOfFile));
  if (!Ok() || !end)
    return Fail();

  return std::make_unique<raw::File>(
      scope.GetSourceElement(), end.value(), std::move(library_decl), std::move(alias_list),
      std::move(using_list), std::move(bits_declaration_list), std::move(const_declaration_list),
      std::move(enum_declaration_list), std::move(protocol_declaration_list),
      std::move(resource_declaration_list), std::move(service_declaration_list),
      std::move(struct_declaration_list), std::move(table_declaration_list),
      std::move(union_declaration_list), std::move(type_decls), std::move(tokens_),
      std::move(comment_tokens_), fidl::utils::Syntax::kOld);
}

std::unique_ptr<raw::LayoutParameter> Parser::ParseLayoutParameter() {
  ASTScope scope(this);

  switch (Peek().combined()) {
  TOKEN_LITERAL_CASES : {
    auto literal = ParseLiteral();
    if (!Ok())
      return Fail();
    auto constant = std::make_unique<raw::LiteralConstant>(std::move(literal));
    return std::make_unique<raw::LiteralLayoutParameter>(scope.GetSourceElement(),
                                                         std::move(constant));
  }
  default: {
    auto type_ctor = ParseTypeConstructorNew();
    if (!Ok())
      return Fail();

    // For non-anonymous type constructors like "foo<T>" or "foo:optional," the presence of type
    // parameters and constraints, respectively, confirms that "foo" refers to a type reference.
    // In cases with no type parameters or constraints present (ie, just "foo"), it is impossible
    // to deduce whether "foo" refers to a type or a value.  In such cases, we must discard the
    // recently built type constructor, and convert it to a compound identifier instead.
    if (type_ctor->layout_ref->kind == raw::LayoutReference::Kind::kNamed &&
        type_ctor->parameters == nullptr && type_ctor->constraints == nullptr) {
      auto named_ref = static_cast<raw::NamedLayoutReference*>(type_ctor->layout_ref.get());
      return std::make_unique<raw::IdentifierLayoutParameter>(scope.GetSourceElement(),
                                                              std::move(named_ref->identifier));
    }
    return std::make_unique<raw::TypeLayoutParameter>(scope.GetSourceElement(),
                                                      std::move(type_ctor));
  }
  }
}

std::unique_ptr<raw::LayoutParameterList> Parser::MaybeParseLayoutParameterList() {
  ASTScope scope(this);
  if (!MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
    return nullptr;
  }

  std::vector<std::unique_ptr<raw::LayoutParameter>> params;
  for (;;) {
    params.emplace_back(ParseLayoutParameter());
    if (!Ok())
      return Fail();
    if (!MaybeConsumeToken(OfKind(Token::Kind::kComma)))
      break;
  }

  ConsumeTokenOrRecover(OfKind(Token::Kind::kRightAngle));
  return std::make_unique<raw::LayoutParameterList>(scope.GetSourceElement(), std::move(params));
}

std::unique_ptr<raw::TypeConstraints> Parser::ParseTypeConstraints() {
  ASTScope scope(this);
  bool bracketed = false;
  std::vector<std::unique_ptr<raw::Constant>> constraints;
  if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
    bracketed = true;
  }

  for (;;) {
    constraints.emplace_back(ParseConstant());
    if (!Ok())
      return Fail();
    if (!bracketed)
      break;
    if (!MaybeConsumeToken(OfKind(Token::Kind::kComma)))
      break;
  }

  if (bracketed) {
    ConsumeTokenOrRecover(OfKind(Token::Kind::kRightAngle));
  } else {
    assert(constraints.size() == 1 &&
           "Compiler bug: only parse one constraint when no brackets present");
  }
  return std::make_unique<raw::TypeConstraints>(scope.GetSourceElement(), std::move(constraints));
}

std::unique_ptr<raw::LayoutMember> Parser::ParseLayoutMember(raw::LayoutMember::Kind kind) {
  ASTScope scope(this);

  auto attributes = MaybeParseAttributeListNew();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::Ordinal64> ordinal = nullptr;
  std::unique_ptr<raw::Identifier> identifier = nullptr;
  if (kind == raw::LayoutMember::Kind::kOrdinaled) {
    ordinal = ParseOrdinal64();
    if (!Ok())
      return Fail();

    bool identifier_is_reserved = Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kReserved);
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();

    if (identifier_is_reserved && Peek().kind() == Token::Kind::kSemicolon) {
      if (attributes != nullptr)
        return Fail(ErrCannotAttachAttributesToReservedOrdinals);
      return std::make_unique<raw::OrdinaledLayoutMember>(
          scope.GetSourceElement(), std::move(attributes), std::move(ordinal));
    }
  }

  if (identifier == nullptr) {
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
  }

  std::unique_ptr<raw::TypeConstructorNew> layout = nullptr;
  if (kind != raw::LayoutMember::Kind::kValue) {
    layout = ParseTypeConstructorNew();
    if (!Ok())
      return Fail();
  }

  // An equal sign followed by a constant (aka, a default value) is optional for
  // a struct member, but required for a value member.
  std::unique_ptr<raw::Constant> value = nullptr;
  if (kind == raw::LayoutMember::Kind::kStruct && MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
    if (!Ok())
      return Fail();
    value = ParseConstant();
    if (!Ok())
      return Fail();
  } else if (kind == raw::LayoutMember::Kind::kValue) {
    ConsumeToken(OfKind(Token::Kind::kEqual));
    if (!Ok())
      return Fail();

    value = ParseConstant();
    if (!Ok())
      return Fail();
  }

  switch (kind) {
    case raw::LayoutMember::kOrdinaled: {
      return std::make_unique<raw::OrdinaledLayoutMember>(scope.GetSourceElement(),
                                                          std::move(attributes), std::move(ordinal),
                                                          std::move(identifier), std::move(layout));
    }
    case raw::LayoutMember::kStruct: {
      return std::make_unique<raw::StructLayoutMember>(scope.GetSourceElement(),
                                                       std::move(attributes), std::move(identifier),
                                                       std::move(layout), std::move(value));
    }
    case raw::LayoutMember::kValue: {
      return std::make_unique<raw::ValueLayoutMember>(
          scope.GetSourceElement(), std::move(attributes), std::move(identifier), std::move(value));
    }
  }
}

std::unique_ptr<raw::Layout> Parser::ParseLayout(
    ASTScope& scope, std::unique_ptr<raw::Modifiers> modifiers,
    std::unique_ptr<raw::CompoundIdentifier> compound_identifier,
    std::unique_ptr<raw::TypeConstructorNew> subtype_ctor) {
  raw::Layout::Kind kind;
  raw::LayoutMember::Kind member_kind;

  if (compound_identifier->components.size() != 1) {
    return Fail(ErrInvalidLayoutClass);
  }
  std::unique_ptr<raw::Identifier> identifier = std::move(compound_identifier->components[0]);

  // TODO(fxbug.dev/65978): Once fully transitioned, we will be able to
  // remove token subkinds for struct, union, table, bits, and enum. Or
  // maybe we want to have a 'recognize token subkind' on an identifier
  // instead of doing string comparison directly.
  if (identifier->span().data() == "bits") {
    if (modifiers != nullptr)
      ValidateModifiersNew<types::Strictness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kBits;
    member_kind = raw::LayoutMember::Kind::kValue;
  } else if (identifier->span().data() == "enum") {
    if (modifiers != nullptr)
      ValidateModifiersNew<types::Strictness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kEnum;
    member_kind = raw::LayoutMember::Kind::kValue;
  } else if (identifier->span().data() == "struct") {
    if (modifiers != nullptr)
      ValidateModifiersNew<types::Resourceness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kStruct;
    member_kind = raw::LayoutMember::Kind::kStruct;
  } else if (identifier->span().data() == "table") {
    if (modifiers != nullptr)
      ValidateModifiersNew<types::Resourceness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kTable;
    member_kind = raw::LayoutMember::Kind::kOrdinaled;
  } else if (identifier->span().data() == "union") {
    if (modifiers != nullptr)
      ValidateModifiersNew<types::Strictness, types::Resourceness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kUnion;
    member_kind = raw::LayoutMember::Kind::kOrdinaled;
  } else {
    return Fail(ErrInvalidLayoutClass);
  }

  if (member_kind != raw::LayoutMember::Kind::kValue && subtype_ctor != nullptr) {
    return Fail(ErrCannotSpecifySubtype, identifier->start_.kind_and_subkind());
  }

  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  std::vector<std::unique_ptr<raw::LayoutMember>> members;
  auto parse_member = [&]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    }
    add(&members, [&] { return ParseLayoutMember(member_kind); });
    return More;
  };

  auto checkpoint = reporter_->Checkpoint();
  while (parse_member() == More) {
    if (!Ok()) {
      const auto result = RecoverToEndOfMember();
      if (result == RecoverResult::Failure) {
        return Fail();
      }
      if (result == RecoverResult::EndOfScope) {
        continue;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }
  if (!Ok())
    return Fail();

  // avoid returning a empty type related errors if there was an error while
  // parsing the members
  if (!checkpoint.NoNewErrors())
    return nullptr;

  if (member_kind == raw::LayoutMember::Kind::kValue && members.empty())
    return Fail(ErrMustHaveOneMember);

  if (kind == raw::Layout::Kind::kUnion) {
    bool contains_non_reserved_member = false;
    for (const std::unique_ptr<raw::LayoutMember>& member : members) {
      assert(member->kind == raw::LayoutMember::Kind::kOrdinaled &&
             "unions should only have ordinaled members");
      const auto& union_member = static_cast<raw::OrdinaledLayoutMember*>(member.get());
      if (!union_member->reserved)
        contains_non_reserved_member = true;
    }
    if (!contains_non_reserved_member)
      return Fail(ErrMustHaveNonReservedMember);
  }

  return std::make_unique<raw::Layout>(scope.GetSourceElement(), kind, std::move(members),
                                       std::move(modifiers), std::move(subtype_ctor));
}

// The colon character is ambiguous. Consider the following two examples:
//
//   type A = enum : foo { BAR = 1; };
//   type B = enum : foo;
//
// When the parser encounters the colon in each case, it has no idea whether the
// value immediately after it should be interpreted as the wrapped type in an
// inline layout of kind enum, or otherwise as the only constraint on a named
// layout called "enum."
//
// To resolve this confusion, we parse the token after the colon as a constant,
// then check to see if the token after that is a left curly brace. If it is, we
// assume that this is in fact the inline layout case ("type A"). If it is not,
// we assume that it is a named layout with constraints ("type B"). If a parse
// failure occurs, std::monostate is returned.
raw::ConstraintOrSubtype Parser::ParseTokenAfterColon() {
  std::unique_ptr<raw::TypeConstructorNew> type_ctor;
  std::unique_ptr<raw::TypeConstraints> constraints;
  ConsumeToken(OfKind(Token::Kind::kColon));
  if (!Ok()) {
    Fail();
    return std::monostate();
  }
  ASTScope scope(this);

  // If the token after the colon is the opener to a constraints list, we know
  // for sure that the identifier before the colon must be a
  // NamedLayoutReference, so none of the other checks in this case are
  // required.
  if (Peek().kind() == Token::Kind::kLeftAngle) {
    return constraints;
  }

  std::unique_ptr<raw::Constant> constraint_or_subtype = ParseConstant();
  if (!Ok()) {
    Fail();
    return std::monostate();
  }

  // If the token after the constant is not an open brace, this was actually a
  // one-entry constraints block the whole time, so it should be parsed as such.
  if (Peek().kind() != Token::Kind::kLeftCurly) {
    std::vector<std::unique_ptr<raw::Constant>> components;
    components.emplace_back(std::move(constraint_or_subtype));
    return std::make_unique<raw::TypeConstraints>(scope.GetSourceElement(), std::move(components));
  }

  // The token we just parsed as a constant is in fact a layout subtype. Coerce
  // it into that class.
  if (constraint_or_subtype->kind != raw::Constant::Kind::kIdentifier) {
    Fail(ErrInvalidWrappedType);
    return std::monostate();
  }

  auto subtype_element =
      raw::SourceElement(constraint_or_subtype->start_, constraint_or_subtype->end_);
  auto subtype_constant = static_cast<raw::IdentifierConstant*>(constraint_or_subtype.get());
  auto subtype_ref = std::make_unique<raw::NamedLayoutReference>(
      subtype_element, std::move(subtype_constant->identifier));
  return std::make_unique<raw::TypeConstructorNew>(subtype_element, std::move(subtype_ref),
                                                   /*parameters=*/nullptr, /*constraints=*/nullptr);
}

using NamedOrInline =
    std::variant<std::unique_ptr<raw::CompoundIdentifier>, std::unique_ptr<raw::Layout>>;

// [ name | { ... } ][ < ... > ][ : ... ]
std::unique_ptr<raw::TypeConstructorNew> Parser::ParseTypeConstructorNew() {
  ASTScope scope(this);
  std::unique_ptr<raw::LayoutReference> layout_ref;
  std::unique_ptr<raw::LayoutParameterList> parameters;
  std::unique_ptr<raw::TypeConstraints> constraints;
  NamedOrInline layout;
  auto attributes = MaybeParseAttributeListNew();

  // Everything except for the (optional) attributes at the start of the type constructor
  // declaration is placed in its own scope.  This is done because in cases of type-level attributes
  // like this
  //
  // «@foo @bar «struct MyStruct { ... }»»;
  //
  // the start and end of the type_ctor and layout SourceElements should begin before and after the
  // attributes block, respectively.
  {
    ASTScope layout_scope(this);
    bool resourceness_comes_first = false;
    std::unique_ptr<raw::Modifiers> modifiers;
    std::unique_ptr<raw::CompoundIdentifier> identifier;
    std::optional<types::Strictness> maybe_strictness = std::nullopt;
    std::optional<Token> maybe_strictness_token = std::nullopt;
    std::optional<types::Resourceness> maybe_resourceness = std::nullopt;
    std::optional<Token> maybe_resourceness_token = std::nullopt;

    // Consume tokens until we get one that isn't a modifier, treating duplicates
    // and conflicts as immediately recovered errors. For conflicts (e.g. "strict
    // flexible" or "flexible strict"), we use the earliest one.
    for (;;) {
      if (Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kStrict) ||
          Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kFlexible) ||
          Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kResource)) {
        ASTScope maybe_compound_identifier_scope(this);
        auto modifier_subkind = Peek().subkind();
        auto maybe_modifier = ParseIdentifier();
        if (!Ok())
          return Fail();

        // Special case: this is either a reference to a type named "flexible/strict/resource" (ex:
        // `struct { foo resource; };`, or otherwise the first modifier on an inline type definition
        // (ex: `struct { foo resource union {...}; };`).  The only way to decide which is which is
        // to peek ahead: if the next token is not an identifier, we assume that the last parsed
        // modifier is actually the identifier of a named value instead.  For example, if the next
        // token after this one isn't an identifier, we're looking at something like:
        //
        //   strict resource;
        //
        // If that's the case, the user is referencing a type named "flexible/strict/resource." This
        // will need special handling to properly reclassify this modifier as the identifier for the
        // whole TypeConstructorNew being built here.
        if (Peek().kind() != Token::kIdentifier) {
          // Looks like we're dealing with named layout reference that has unfortunately been named
          // "flexible/strict/resource."
          identifier =
              ParseCompoundIdentifier(maybe_compound_identifier_scope, std::move(maybe_modifier));
          break;
        }

        Token& modifier_token = maybe_modifier->start_;
        switch (modifier_subkind) {
          case Token::Subkind::kFlexible:
          case Token::Subkind::kStrict: {
            auto as_strictness = modifier_subkind == Token::Subkind::kFlexible
                                     ? types::Strictness::kFlexible
                                     : types::Strictness::kStrict;
            if (maybe_strictness == as_strictness) {
              Fail(ErrDuplicateModifier, modifier_token, modifier_token.kind_and_subkind());
              RecoverOneError();
              break;
            }
            if (maybe_strictness.has_value()) {
              Fail(ErrConflictingModifier, modifier_token, modifier_token.kind_and_subkind(),
                   Token::KindAndSubkind(Token::Kind::kIdentifier,
                                         modifier_subkind == Token::Subkind::kFlexible
                                             ? Token::Subkind::kStrict
                                             : Token::Subkind::kFlexible));
              RecoverOneError();
              break;
            }
            maybe_strictness = as_strictness;
            maybe_strictness_token = maybe_modifier->start_;
            break;
          }
          case Token::Subkind::kResource: {
            if (maybe_resourceness == types::Resourceness::kResource) {
              Fail(ErrDuplicateModifier, modifier_token, modifier_token.kind_and_subkind());
              RecoverOneError();
              break;
            }
            if (maybe_strictness == std::nullopt) {
              resourceness_comes_first = true;
            }
            maybe_resourceness = types::Resourceness::kResource;
            maybe_resourceness_token = maybe_modifier->start_;
            break;
          }
          default: {
            assert(false && "expected modifier token");
          }
        }
      } else {
        if (maybe_strictness.has_value() || maybe_resourceness.has_value()) {
          modifiers = std::make_unique<raw::Modifiers>(
              layout_scope.GetSourceElement(), maybe_resourceness, maybe_resourceness_token,
              maybe_strictness, maybe_strictness_token, resourceness_comes_first);
        }
        break;
      }
    }

    // Any type constructor which is not a reference to a type named "flexible/strict/resource" will
    // have the identifier unset, and will enter the block below to parse it.
    if (identifier == nullptr) {
      identifier = ParseCompoundIdentifier();
      if (!Ok())
        return Fail();
    }

    switch (Peek().kind()) {
      case Token::Kind::kLeftCurly: {
        layout = ParseLayout(layout_scope, std::move(modifiers), std::move(identifier),
                             /*subtype_ctor=*/nullptr);
        if (!Ok())
          return Fail();
        break;
      }
      case Token::Kind::kColon: {
        raw::ConstraintOrSubtype after_colon = ParseTokenAfterColon();
        std::visit(fidl::utils::matchers{
                       [&](std::unique_ptr<raw::TypeConstraints>& constraint) -> void {
                         if (modifiers != nullptr)
                           ValidateModifiersNew</* none */>(modifiers, identifier->start_);
                         if (attributes != nullptr)
                           Fail(ErrCannotAttachAttributeToIdentifier, attributes->span());
                         constraints = std::move(constraint);
                         layout = std::move(identifier);
                       },
                       [&](std::unique_ptr<raw::TypeConstructorNew>& type_ctor) -> void {
                         layout = ParseLayout(layout_scope, std::move(modifiers),
                                              std::move(identifier), std::move(type_ctor));
                         if (!Ok()) {
                           Fail();
                         }
                       },
                       [&](std::monostate _) -> void { assert(!Ok()); },
                   },
                   after_colon);

        if (!Ok()) {
          return nullptr;
        }
        break;
      }
      default: {
        if (modifiers != nullptr)
          ValidateModifiersNew</* none */>(modifiers, identifier->start_);
        if (attributes != nullptr)
          Fail(ErrCannotAttachAttributeToIdentifier, attributes->span());
        layout = std::move(identifier);
      }
    }
  }

  // Build a LayoutReference of the right type based on the underlying type of the layout.
  assert((std::holds_alternative<std::unique_ptr<raw::CompoundIdentifier>>(layout) ||
          std::holds_alternative<std::unique_ptr<raw::Layout>>(layout)) &&
         "must have set layout by this point");
  std::visit(fidl::utils::matchers{
                 [&](std::unique_ptr<raw::CompoundIdentifier>& named_layout) -> void {
                   layout_ref = std::make_unique<raw::NamedLayoutReference>(
                       raw::SourceElement(named_layout->start_, named_layout->end_),
                       std::move(named_layout));
                 },
                 [&](std::unique_ptr<raw::Layout>& inline_layout) -> void {
                   layout_ref = std::make_unique<raw::InlineLayoutReference>(
                       scope.GetSourceElement(), std::move(attributes), std::move(inline_layout));
                 },
             },
             layout);

  if (previous_token_.kind() != Token::Kind::kColon) {
    parameters = MaybeParseLayoutParameterList();
    if (!Ok())
      return Fail();
  }

  MaybeConsumeToken(OfKind(Token::Kind::kColon));
  if (previous_token_.kind() == Token::Kind::kColon) {
    constraints = ParseTypeConstraints();
    if (!Ok())
      return Fail();
  }

  assert(layout_ref != nullptr &&
         "ParseTypeConstructorNew must always produce a non-null layout_ref");
  return std::make_unique<raw::TypeConstructorNew>(scope.GetSourceElement(), std::move(layout_ref),
                                                   std::move(parameters), std::move(constraints));
}

raw::TypeConstructor Parser::ParseTypeConstructor() {
  if (syntax_ == fidl::utils::Syntax::kNew)
    return ParseTypeConstructorNew();
  return ParseTypeConstructorOld();
}

std::unique_ptr<raw::TypeDecl> Parser::ParseTypeDecl(
    std::unique_ptr<raw::AttributeListNew> attributes, ASTScope& scope) {
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kType));
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kEqual));
  if (!Ok())
    return Fail();

  auto layout = ParseTypeConstructorNew();
  if (!Ok())
    return Fail();

  bool layout_has_attributes =
      layout->layout_ref->kind == raw::LayoutReference::Kind::kInline &&
      static_cast<raw::InlineLayoutReference*>(layout->layout_ref.get())->attributes != nullptr;
  if (attributes != nullptr && layout_has_attributes)
    return Fail(ErrRedundantAttributePlacement, scope.GetSourceElement().span());
  return std::make_unique<raw::TypeDecl>(scope.GetSourceElement(), std::move(attributes),
                                         std::move(identifier), std::move(layout));
}

std::unique_ptr<raw::File> Parser::ParseFileNewSyntax(
    ASTScope& scope, std::unique_ptr<raw::LibraryDecl> library_decl) {
  std::vector<std::unique_ptr<raw::AliasDeclaration>> alias_list;
  std::vector<std::unique_ptr<raw::Using>> using_list;
  std::vector<std::unique_ptr<raw::BitsDeclaration>> bits_declaration_list;
  std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
  std::vector<std::unique_ptr<raw::EnumDeclaration>> enum_declaration_list;
  std::vector<std::unique_ptr<raw::ProtocolDeclaration>> protocol_declaration_list;
  std::vector<std::unique_ptr<raw::ResourceDeclaration>> resource_declaration_list;
  std::vector<std::unique_ptr<raw::ServiceDeclaration>> service_declaration_list;
  std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;
  std::vector<std::unique_ptr<raw::TableDeclaration>> table_declaration_list;
  std::vector<std::unique_ptr<raw::UnionDeclaration>> union_declaration_list;
  std::vector<std::unique_ptr<raw::TypeDecl>> type_decls;

  bool done_with_library_imports = false;
  auto parse_declaration = [&]() {
    // TODO(fxbug.dev/70247): Once we're fully on the new syntax, we should refactor all of the
    //  top-level "Parse..." methods to omit their externally defined ASTScope parameter.  This was
    //  necessary when top-level definitions could begin with modifiers (ex: "strict struct S {...")
    //  which is no longer possible in the new syntax.
    ASTScope scope(this);
    std::unique_ptr<raw::AttributeListNew> attributes = MaybeParseAttributeListNew();
    if (!Ok())
      return More;

    switch (Peek().combined()) {
      default:
        Fail(ErrExpectedDeclaration, last_token_.data());
        return More;

      case CASE_TOKEN(Token::Kind::kEndOfFile):
        return Done;

      case CASE_IDENTIFIER(Token::Subkind::kDeprecatedSyntax): {
        Fail(ErrMisplacedSyntaxVersion);
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kAlias): {
        done_with_library_imports = true;
        add(&alias_list,
            [&] { return ParseAliasDeclaration(std::move(attributes), scope, Modifiers()); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kConst): {
        done_with_library_imports = true;
        add(&const_declaration_list,
            [&] { return ParseConstDeclaration(std::move(attributes), scope, Modifiers()); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kType): {
        done_with_library_imports = true;
        add(&type_decls, [&] { return ParseTypeDecl(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kProtocol): {
        done_with_library_imports = true;
        add(&protocol_declaration_list,
            [&] { return ParseProtocolDeclaration(std::move(attributes), scope, Modifiers()); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kResourceDefinition): {
        done_with_library_imports = true;
        add(&resource_declaration_list,
            [&] { return ParseResourceDeclaration(std::move(attributes), scope, Modifiers()); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kService): {
        done_with_library_imports = true;
        add(&service_declaration_list,
            [&] { return ParseServiceDeclaration(std::move(attributes), scope, Modifiers()); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kUsing): {
        add(&using_list, [&] { return ParseUsing(std::move(attributes), scope, Modifiers()); });
        if (Ok() && done_with_library_imports) {
          reporter_->Report(diagnostics::ErrLibraryImportsMustBeGroupedAtTopOfFile,
                            using_list.back()->span());
        }
        return More;
      }
    }
  };

  while (parse_declaration() == More) {
    if (!Ok()) {
      // If this returns RecoverResult::Continue, we have consumed up to a '}'
      // and expect a ';' to follow.
      auto result = RecoverToEndOfDecl();
      if (result == RecoverResult::Failure) {
        return Fail();
      } else if (result == RecoverResult::EndOfScope) {
        break;
      }
    }
    ConsumeTokenOrRecover(OfKind(Token::Kind::kSemicolon));
  }

  std::optional<Token> end = ConsumeToken(OfKind(Token::Kind::kEndOfFile));
  if (!Ok() || !end)
    return Fail();

  return std::make_unique<raw::File>(
      scope.GetSourceElement(), end.value(), std::move(library_decl), std::move(alias_list),
      std::move(using_list), std::move(bits_declaration_list), std::move(const_declaration_list),
      std::move(enum_declaration_list), std::move(protocol_declaration_list),
      std::move(resource_declaration_list), std::move(service_declaration_list),
      std::move(struct_declaration_list), std::move(table_declaration_list),
      std::move(union_declaration_list), std::move(type_decls), std::move(tokens_),
      std::move(comment_tokens_), fidl::utils::Syntax::kNew);
}

bool Parser::ConsumeTokensUntil(std::set<Token::Kind> exit_tokens) {
  auto p = [&](Token::KindAndSubkind token) -> std::unique_ptr<Diagnostic> {
    for (const auto& exit_token : exit_tokens) {
      if (token.kind() == exit_token)
        // signal to ReadToken to stop by returning an error
        return Reporter::MakeError(ErrUnexpectedToken);
    }
    // nullptr return value indicates -> yes, consume to ReadToken
    return nullptr;
  };

  // Consume tokens until we find a synchronization point
  while (ReadToken(p, OnNoMatch::kIgnore) != std::nullopt) {
    if (!Ok())
      return false;
  }
  return true;
}

Parser::RecoverResult Parser::RecoverToEndOfAttributeNew() {
  if (ConsumedEOF()) {
    return RecoverResult::Failure;
  }

  RecoverAllErrors();

  static const auto exit_tokens = std::set<Token::Kind>{
      Token::Kind::kRightParen,
      Token::Kind::kEndOfFile,
  };
  if (!ConsumeTokensUntil(exit_tokens)) {
    return RecoverResult::Failure;
  }

  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kRightParen):
      ConsumeToken(OfKind(Token::Kind::kRightParen));
      if (!Ok())
        return RecoverResult::Failure;
      return RecoverResult::Continue;
    case CASE_TOKEN(Token::Kind::kEndOfFile):
      return RecoverResult::EndOfScope;
    default:
      return RecoverResult::Failure;
  }
}

Parser::RecoverResult Parser::RecoverToEndOfDecl() {
  if (ConsumedEOF()) {
    return RecoverResult::Failure;
  }

  RecoverAllErrors();

  static const auto exit_tokens = std::set<Token::Kind>{
      Token::Kind::kRightCurly,
      Token::Kind::kEndOfFile,
  };
  if (!ConsumeTokensUntil(exit_tokens)) {
    return RecoverResult::Failure;
  }

  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kRightCurly):
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      if (!Ok())
        return RecoverResult::Failure;
      return RecoverResult::Continue;
    case CASE_TOKEN(Token::Kind::kEndOfFile):
      return RecoverResult::EndOfScope;
    default:
      return RecoverResult::Failure;
  }
}

Parser::RecoverResult Parser::RecoverToEndOfMember() {
  if (ConsumedEOF()) {
    return RecoverResult::Failure;
  }

  RecoverAllErrors();

  static const auto exit_tokens = std::set<Token::Kind>{
      Token::Kind::kSemicolon,
      Token::Kind::kRightCurly,
      Token::Kind::kEndOfFile,
  };
  if (!ConsumeTokensUntil(exit_tokens)) {
    return RecoverResult::Failure;
  }

  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kSemicolon):
      return RecoverResult::Continue;
    case CASE_TOKEN(Token::Kind::kRightCurly):
      return RecoverResult::EndOfScope;
    default:
      return RecoverResult::Failure;
  }
}

template <Token::Kind ClosingToken>
Parser::RecoverResult Parser::RecoverToEndOfListItem() {
  if (ConsumedEOF()) {
    return RecoverResult::Failure;
  }

  RecoverAllErrors();

  static const auto exit_tokens = std::set<Token::Kind>{
      Token::Kind::kComma,
      Token::Kind::kSemicolon,
      Token::Kind::kRightCurly,
      Token::Kind::kEndOfFile,
      ClosingToken,
  };
  if (!ConsumeTokensUntil(exit_tokens)) {
    return RecoverResult::Failure;
  }

  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kComma):
      return RecoverResult::Continue;
    case CASE_TOKEN(ClosingToken):
      return RecoverResult::EndOfScope;
    default:
      return RecoverResult::Failure;
  }
}

Parser::RecoverResult Parser::RecoverToEndOfAttributeArg() {
  return RecoverToEndOfListItem<Token::Kind::kRightParen>();
}

Parser::RecoverResult Parser::RecoverToEndOfParam() {
  return RecoverToEndOfListItem<Token::Kind::kRightParen>();
}

Parser::RecoverResult Parser::RecoverToEndOfParamList() {
  if (ConsumedEOF()) {
    return RecoverResult::Failure;
  }

  RecoverAllErrors();

  static const auto exit_tokens = std::set<Token::Kind>{
      Token::Kind::kRightParen,
  };
  if (!ConsumeTokensUntil(exit_tokens)) {
    return RecoverResult::Failure;
  }

  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kRightParen):
      return RecoverResult::EndOfScope;
    default:
      return RecoverResult::Failure;
  }
}

}  // namespace fidl
