// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/parser.h"

#include <errno.h>
#include <lib/fit/function.h>
#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/experimental_flags.h"
#include "tools/fidl/fidlc/include/fidl/types.h"
#include "tools/fidl/fidlc/include/fidl/utils.h"

namespace fidl {

// The "case" keyword is not folded into CASE_TOKEN and CASE_IDENTIFIER because
// doing so confuses clang-format.
#define CASE_TOKEN(K) Token::KindAndSubkind(K, Token::Subkind::kNone).combined()

#define CASE_IDENTIFIER(K) Token::KindAndSubkind(Token::Kind::kIdentifier, K).combined()

#define TOKEN_TYPE_CASES                          \
  case CASE_IDENTIFIER(Token::Subkind::kNone):    \
  case CASE_IDENTIFIER(Token::Subkind::kArray):   \
  case CASE_IDENTIFIER(Token::Subkind::kVector):  \
  case CASE_IDENTIFIER(Token::Subkind::kString):  \
  case CASE_IDENTIFIER(Token::Subkind::kRequest): \
  case CASE_IDENTIFIER(Token::Subkind::kZxExperimentalPointer)

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
      checkpoint_(reporter->Checkpoint()),
      experimental_flags_(experimental_flags),
      state_(State::kNormal) {
  last_token_ = Lex();
}

std::nullptr_t Parser::Fail() { return Fail(ErrUnexpectedToken); }

template <ErrorId Id, typename... Args>
std::nullptr_t Parser::Fail(const ErrorDef<Id, Args...>& err, const identity_t<Args>&... args) {
  return Fail(err, last_token_, args...);
}

template <ErrorId Id, typename... Args>
std::nullptr_t Parser::Fail(const ErrorDef<Id, Args...>& err, Token token,
                            const identity_t<Args>&... args) {
  return Fail(err, token.span(), args...);
}

template <ErrorId Id, typename... Args>
std::nullptr_t Parser::Fail(const ErrorDef<Id, Args...>& err, SourceSpan span,
                            const identity_t<Args>&... args) {
  if (Ok()) {
    reporter_->Fail(err, span, args...);
  }
  return nullptr;
}

// TODO(fxbug.dev/108248): Remove once all outstanding errors are documented.
template <ErrorId Id, typename... Args>
std::nullptr_t Parser::Fail(const UndocumentedErrorDef<Id, Args...>& err,
                            const identity_t<Args>&... args) {
  return Fail(err, last_token_, args...);
}
template <ErrorId Id, typename... Args>
std::nullptr_t Parser::Fail(const UndocumentedErrorDef<Id, Args...>& err, Token token,
                            const identity_t<Args>&... args) {
  return Fail(err, token.span(), args...);
}
template <ErrorId Id, typename... Args>
std::nullptr_t Parser::Fail(const UndocumentedErrorDef<Id, Args...>& err, SourceSpan span,
                            const identity_t<Args>&... args) {
  if (Ok()) {
    reporter_->Fail(err, span, args...);
  }
  return nullptr;
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
    return Fail(ErrMissingOrdinalBeforeMember);
  if (!Ok())
    return Fail();
  auto data = scope.GetSourceElement().span().data();
  std::string string_data(data.data(), data.data() + data.size());
  errno = 0;
  unsigned long long value = strtoull(string_data.data(), nullptr, 0);
  ZX_ASSERT_MSG(errno == 0, "unparsable number should not be lexed.");
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

std::unique_ptr<raw::BoolLiteral> Parser::ParseBoolLiteral(Token::Subkind subkind) {
  ASTScope scope(this);
  ConsumeToken(IdentifierOfSubkind(subkind));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::BoolLiteral>(scope.GetSourceElement(),
                                            Token::Subkind::kTrue == subkind);
}

std::unique_ptr<raw::Literal> Parser::ParseLiteral() {
  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kStringLiteral):
      return ParseStringLiteral();

    case CASE_TOKEN(Token::Kind::kNumericLiteral):
      return ParseNumericLiteral();

    case CASE_IDENTIFIER(Token::Subkind::kTrue):
      return ParseBoolLiteral(Token::Subkind::kTrue);

    case CASE_IDENTIFIER(Token::Subkind::kFalse):
      return ParseBoolLiteral(Token::Subkind::kFalse);

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

  return std::make_unique<raw::AttributeArg>(scope.GetSourceElement(), std::move(name),
                                             std::move(value));
}

std::unique_ptr<raw::Attribute> Parser::ParseAttribute() {
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
          return Fail(ErrInvalidIdentifier, maybe_constant->span().data());
        }
        auto constant = static_cast<raw::IdentifierConstant*>(maybe_constant.get());
        if (constant->identifier->components.size() > 1) {
          return Fail(ErrInvalidIdentifier, maybe_constant->span().data());
        }

        ConsumeToken(OfKind(Token::Kind::kEqual));
        if (!Ok())
          return Fail();

        auto arg_name = std::move(constant->identifier->components.front());
        auto value = ParseConstant();
        if (!Ok())
          return Fail();

        args.emplace_back(std::make_unique<raw::AttributeArg>(
            arg_scope.GetSourceElement(), std::move(arg_name), std::move(value)));
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

  return std::make_unique<raw::Attribute>(scope.GetSourceElement(), std::move(name),
                                          std::move(args));
}

std::unique_ptr<raw::AttributeList> Parser::ParseAttributeList(
    std::unique_ptr<raw::Attribute> doc_comment, ASTScope& scope) {
  std::vector<std::unique_ptr<raw::Attribute>> attributes;
  if (doc_comment)
    attributes.emplace_back(std::move(doc_comment));

  for (;;) {
    auto attribute = ParseAttribute();
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

  return std::make_unique<raw::AttributeList>(scope.GetSourceElement(), std::move(attributes));
}

std::unique_ptr<raw::Attribute> Parser::ParseDocComment() {
  ASTScope scope(this);
  std::optional<Token> doc_line;
  std::optional<Token> first_doc_line;
  while (Peek().kind() == Token::Kind::kDocComment) {
    if (first_doc_line) {
      // disallow any blank lines between this doc comment and the previous one
      std::string_view trailing_whitespace = last_token_.previous_end().data();
      if (std::count(trailing_whitespace.cbegin(), trailing_whitespace.cend(), '\n') > 1)
        reporter_->Warn(WarnBlankLinesWithinDocCommentBlock, previous_token_.span());
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
    reporter_->Warn(WarnDocCommentMustBeFollowedByDeclaration, previous_token_.span());

  std::vector<std::unique_ptr<raw::AttributeArg>> args;
  args.emplace_back(
      std::make_unique<raw::AttributeArg>(scope.GetSourceElement(), std::move(constant)));

  auto doc_comment_attr =
      raw::Attribute::CreateDocComment(scope.GetSourceElement(), std::move(args));
  return std::make_unique<raw::Attribute>(std::move(doc_comment_attr));
}

std::unique_ptr<raw::AttributeList> Parser::MaybeParseAttributeList() {
  ASTScope scope(this);
  std::unique_ptr<raw::Attribute> doc_comment;
  // Doc comments must appear above attributes
  if (Peek().kind() == Token::Kind::kDocComment) {
    doc_comment = ParseDocComment();
  }
  if (Peek().kind() == Token::Kind::kAt) {
    return ParseAttributeList(std::move(doc_comment), scope);
  }
  // no generic attributes, start the attribute list
  if (doc_comment) {
    std::vector<std::unique_ptr<raw::Attribute>> attributes;
    attributes.emplace_back(std::move(doc_comment));
    return std::make_unique<raw::AttributeList>(scope.GetSourceElement(), std::move(attributes));
  }
  return nullptr;
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

std::unique_ptr<raw::AliasDeclaration> Parser::ParseAliasDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kAlias));
  if (!Ok())
    return Fail();

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

std::unique_ptr<raw::Using> Parser::ParseUsing(std::unique_ptr<raw::AttributeList> attributes,
                                               ASTScope& scope) {
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUsing));
  if (!Ok())
    return Fail();

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

std::unique_ptr<raw::ConstDeclaration> Parser::ParseConstDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kConst));
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();
  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();

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

std::unique_ptr<raw::ParameterList> Parser::ParseParameterList() {
  ASTScope scope(this);
  std::unique_ptr<raw::TypeConstructor> type_ctor;

  ConsumeToken(OfKind(Token::Kind::kLeftParen));
  if (!Ok())
    return Fail();

  if (Peek().kind() != Token::Kind::kRightParen) {
    type_ctor = ParseTypeConstructor();
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
        if (!attrs.empty() && attrs[0]->provenance == raw::Attribute::Provenance::kDocComment) {
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

  return std::make_unique<raw::ParameterList>(scope.GetSourceElement(), std::move(type_ctor));
}

std::unique_ptr<raw::ProtocolMethod> Parser::ParseProtocolEvent(
    std::unique_ptr<raw::AttributeList> attributes, std::unique_ptr<raw::Modifiers> modifiers,
    ASTScope& scope) {
  ConsumeToken(OfKind(Token::Kind::kArrow));
  if (!Ok())
    return Fail();

  auto method_name = ParseIdentifier();
  if (!Ok())
    return Fail();

  auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
    if (!Ok())
      return false;
    *params_out = ParseParameterList();
    return Ok();
  };

  std::unique_ptr<raw::ParameterList> request;
  std::unique_ptr<raw::ParameterList> response;
  if (!parse_params(&response))
    return Fail();

  std::unique_ptr<raw::TypeConstructor> maybe_error;
  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kError))) {
    maybe_error = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  }

  ZX_ASSERT(method_name);
  ZX_ASSERT(response != nullptr);

  return std::make_unique<raw::ProtocolMethod>(
      scope.GetSourceElement(), std::move(attributes), std::move(modifiers), std::move(method_name),
      std::move(request), std::move(response), std::move(maybe_error));
}

std::unique_ptr<raw::ProtocolMethod> Parser::ParseProtocolMethod(
    std::unique_ptr<raw::AttributeList> attributes, std::unique_ptr<raw::Modifiers> modifiers,
    std::unique_ptr<raw::Identifier> method_name, ASTScope& scope) {
  auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
    *params_out = ParseParameterList();
    return Ok();
  };

  std::unique_ptr<raw::ParameterList> request;
  if (!parse_params(&request))
    return Fail();

  std::unique_ptr<raw::ParameterList> maybe_response;
  std::unique_ptr<raw::TypeConstructor> maybe_error;
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

  ZX_ASSERT(method_name);
  ZX_ASSERT(request != nullptr);

  return std::make_unique<raw::ProtocolMethod>(
      scope.GetSourceElement(), std::move(attributes), std::move(modifiers), std::move(method_name),
      std::move(request), std::move(maybe_response), std::move(maybe_error));
}

std::unique_ptr<raw::ProtocolCompose> Parser::ParseProtocolCompose(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
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
  std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
  if (!Ok()) {
    Fail();
    return;
  }

  switch (Peek().kind()) {
    case Token::Kind::kArrow: {
      add(methods, [&] {
        return ParseProtocolEvent(std::move(attributes), /* modifiers= */ nullptr, scope);
      });
      return;
    }
    case Token::Kind::kIdentifier: {
      std::unique_ptr<raw::Modifiers> modifiers;
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
          return;
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
      } else if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions) &&
                 (Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kStrict) ||
                  Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kFlexible))) {
        // There are two possibilities here: we are looking at a method or event with strictness
        // modifier like `strict MyMethod(...);` or we are looking at a method
        // that has unfortunately been named `flexible/strict(...);`. In either case we only expect
        // one identifier, not a compound identifier, so we can just parse the identifier.
        auto modifier_subkind = Peek().subkind();
        auto maybe_modifier = ParseIdentifier();
        if (!Ok()) {
          Fail();
          return;
        }

        if (Peek().kind() == Token::Kind::kLeftParen) {
          // This is actually a method named `strict` or `flexible`.
          method_name = std::move(maybe_modifier);
        } else {
          // This is a modifier on either an event or a method.
          auto as_strictness = modifier_subkind == Token::Subkind::kFlexible
                                   ? types::Strictness::kFlexible
                                   : types::Strictness::kStrict;
          modifiers = std::make_unique<raw::Modifiers>(
              scope.GetSourceElement(),
              raw::Modifier<types::Strictness>(as_strictness, maybe_modifier->start_));
          switch (Peek().kind()) {
            case Token::Kind::kArrow: {
              add(methods, [&] {
                return ParseProtocolEvent(std::move(attributes), std::move(modifiers), scope);
              });
              return;
            }
            case Token::Kind::kIdentifier: {
              method_name = ParseIdentifier();
              if (!Ok()) {
                Fail();
                return;
              }
              if (Peek().kind() != Token::Kind::kLeftParen) {
                Fail(ErrInvalidProtocolMember);
                return;
              }
              break;
            }
            default:
              Fail(ErrInvalidProtocolMember);
              return;
          }
        }
      } else {
        method_name = ParseIdentifier();
        if (!Ok()) {
          Fail();
          return;
        }
        if (Peek().kind() != Token::Kind::kLeftParen) {
          Fail(ErrInvalidProtocolMember);
          return;
        }
      }

      add(methods, [&] {
        return ParseProtocolMethod(std::move(attributes), std::move(modifiers),
                                   std::move(method_name), scope);
      });
      return;
    }
    default:
      Fail(ErrInvalidProtocolMember);
      return;
  }
}

std::unique_ptr<raw::ProtocolDeclaration> Parser::ParseProtocolDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
  std::unique_ptr<raw::Modifiers> modifiers;
  std::vector<std::unique_ptr<raw::ProtocolCompose>> composed_protocols;
  std::vector<std::unique_ptr<raw::ProtocolMethod>> methods;

  if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions) &&
      (Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kOpen) ||
       Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kAjar) ||
       Peek().combined() == CASE_IDENTIFIER(Token::Subkind::kClosed))) {
    auto modifier_subkind = Peek().subkind();
    auto modifier = ParseIdentifier();
    if (!Ok())
      return Fail();

    types::Openness as_openness;
    switch (modifier_subkind) {
      case Token::Subkind::kOpen:
        as_openness = types::Openness::kOpen;
        break;
      case Token::Subkind::kAjar:
        as_openness = types::Openness::kAjar;
        break;
      case Token::Subkind::kClosed:
        as_openness = types::Openness::kClosed;
        break;
      default:
        ZX_PANIC("expected openness token");
    }
    modifiers = std::make_unique<raw::Modifiers>(
        scope.GetSourceElement(), raw::Modifier<types::Openness>(as_openness, modifier->start_));
  }

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kProtocol));
  if (!Ok())
    return Fail();

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
    }
    ParseProtocolMember(&composed_protocols, &methods);
    return More;
  };

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
    Fail();

  return std::make_unique<raw::ProtocolDeclaration>(
      scope.GetSourceElement(), std::move(attributes), std::move(modifiers), std::move(identifier),
      std::move(composed_protocols), std::move(methods));
}

std::unique_ptr<raw::ResourceProperty> Parser::ParseResourcePropertyDeclaration() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();
  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::ResourceProperty>(scope.GetSourceElement(), std::move(type_ctor),
                                                 std::move(identifier), std::move(attributes));
}

std::unique_ptr<raw::ResourceDeclaration> Parser::ParseResourceDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
  std::vector<std::unique_ptr<raw::ResourceProperty>> properties;

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kResourceDefinition));
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::TypeConstructor> maybe_type_ctor;
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    ASTScope type_identifier_scope(this);
    auto resource_type_identifier = ParseCompoundIdentifier();
    if (!Ok())
      return Fail();

    maybe_type_ctor = std::make_unique<raw::TypeConstructor>(
        scope.GetSourceElement(),
        std::make_unique<raw::NamedLayoutReference>(type_identifier_scope.GetSourceElement(),
                                                    std::move(resource_type_identifier)),
        /*parameters=*/nullptr,
        /*constraints=*/nullptr);
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
    }
    add(&properties, [&] { return ParseResourcePropertyDeclaration(); });
    return More;
  };

  auto checkpoint = reporter_->Checkpoint();
  while (parse_prop() == More) {
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

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();
  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::ServiceMember>(scope.GetSourceElement(), std::move(type_ctor),
                                              std::move(identifier), std::move(attributes));
}

std::unique_ptr<raw::ServiceDeclaration> Parser::ParseServiceDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
  std::vector<std::unique_ptr<raw::ServiceMember>> members;

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kService));
  if (!Ok())
    return Fail();

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
    }
    add(&members, [&] { return ParseServiceMember(); });
    return More;
  };

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
    Fail();

  return std::make_unique<raw::ServiceDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                   std::move(identifier), std::move(members));
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
    auto type_ctor = ParseTypeConstructor();
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

  if (!ConsumeToken(OfKind(Token::Kind::kRightAngle)))
    return Fail();

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
    ZX_ASSERT_MSG(constraints.size() == 1, "only parse one constraint when no brackets present");
  }
  return std::make_unique<raw::TypeConstraints>(scope.GetSourceElement(), std::move(constraints));
}

std::unique_ptr<raw::LayoutMember> Parser::ParseLayoutMember(raw::LayoutMember::Kind kind) {
  ASTScope scope(this);

  auto attributes = MaybeParseAttributeList();
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
      return std::make_unique<raw::OrdinaledLayoutMember>(
          scope.GetSourceElement(), std::move(attributes), std::move(ordinal));
    }
  }

  if (identifier == nullptr) {
    identifier = ParseIdentifier();
    if (!Ok())
      return Fail();
  }

  std::unique_ptr<raw::TypeConstructor> layout = nullptr;
  if (kind != raw::LayoutMember::Kind::kValue) {
    layout = ParseTypeConstructor();
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
    std::unique_ptr<raw::TypeConstructor> subtype_ctor) {
  raw::Layout::Kind kind;
  raw::LayoutMember::Kind member_kind;

  if (compound_identifier->components.size() != 1) {
    return Fail(ErrInvalidLayoutClass);
  }
  std::unique_ptr<raw::Identifier> identifier = std::move(compound_identifier->components[0]);

  if (identifier->span().data() == "bits") {
    if (modifiers != nullptr)
      ValidateModifiers<types::Strictness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kBits;
    member_kind = raw::LayoutMember::Kind::kValue;
  } else if (identifier->span().data() == "enum") {
    if (modifiers != nullptr)
      ValidateModifiers<types::Strictness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kEnum;
    member_kind = raw::LayoutMember::Kind::kValue;
  } else if (identifier->span().data() == "struct") {
    if (modifiers != nullptr)
      ValidateModifiers<types::Resourceness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kStruct;
    member_kind = raw::LayoutMember::Kind::kStruct;
  } else if (identifier->span().data() == "table") {
    if (modifiers != nullptr)
      ValidateModifiers<types::Resourceness>(modifiers, identifier->start_);
    kind = raw::Layout::Kind::kTable;
    member_kind = raw::LayoutMember::Kind::kOrdinaled;
  } else if (identifier->span().data() == "union") {
    if (modifiers != nullptr)
      ValidateModifiers<types::Strictness, types::Resourceness>(modifiers, identifier->start_);
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
  std::unique_ptr<raw::TypeConstructor> type_ctor;
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
  return std::make_unique<raw::TypeConstructor>(subtype_element, std::move(subtype_ref),
                                                /*parameters=*/nullptr, /*constraints=*/nullptr);
}

using NamedOrInline =
    std::variant<std::unique_ptr<raw::CompoundIdentifier>, std::unique_ptr<raw::Layout>>;

// [ name | { ... } ][ < ... > ][ : ... ]
std::unique_ptr<raw::TypeConstructor> Parser::ParseTypeConstructor() {
  ASTScope scope(this);
  std::unique_ptr<raw::LayoutReference> layout_ref;
  std::unique_ptr<raw::LayoutParameterList> parameters;
  std::unique_ptr<raw::TypeConstraints> constraints;
  NamedOrInline layout;
  auto attributes = MaybeParseAttributeList();

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
    std::optional<raw::Modifier<types::Strictness>> maybe_strictness = std::nullopt;
    std::optional<raw::Modifier<types::Resourceness>> maybe_resourceness = std::nullopt;

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
        // `struct { foo resource; };`), or otherwise the first modifier on an inline type
        // definition (ex: `struct { foo resource union {...}; };`).  The only way to decide which
        // is which is to peek ahead: if the next token is not an identifier, we assume that the
        // last parsed modifier is actually the identifier of a named value instead.  For example,
        // if the next token after this one isn't an identifier, we're looking at something like:
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
            if (maybe_strictness.has_value() && maybe_strictness->value == as_strictness) {
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
            maybe_strictness.emplace(as_strictness, maybe_modifier->start_);
            break;
          }
          case Token::Subkind::kResource: {
            if (maybe_resourceness.has_value() &&
                maybe_resourceness->value == types::Resourceness::kResource) {
              Fail(ErrDuplicateModifier, modifier_token, modifier_token.kind_and_subkind());
              RecoverOneError();
              break;
            }
            if (maybe_strictness == std::nullopt) {
              resourceness_comes_first = true;
            }
            maybe_resourceness.emplace(types::Resourceness::kResource, maybe_modifier->start_);
            break;
          }
          default: {
            ZX_PANIC("expected modifier token");
          }
        }
      } else {
        if (maybe_strictness.has_value() || maybe_resourceness.has_value()) {
          modifiers =
              std::make_unique<raw::Modifiers>(layout_scope.GetSourceElement(), maybe_resourceness,
                                               maybe_strictness, resourceness_comes_first);
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
                         if (constraints != nullptr) {
                           Fail(ErrMultipleConstraintDefinitions, previous_token_.span());
                         }
                         if (modifiers != nullptr)
                           ValidateModifiers</* none */>(modifiers, identifier->start_);
                         if (attributes != nullptr)
                           Fail(ErrCannotAttachAttributeToIdentifier, attributes->span());
                         constraints = std::move(constraint);
                         layout = std::move(identifier);
                       },
                       [&](std::unique_ptr<raw::TypeConstructor>& type_ctor) -> void {
                         layout = ParseLayout(layout_scope, std::move(modifiers),
                                              std::move(identifier), std::move(type_ctor));
                         if (!Ok()) {
                           Fail();
                         }
                       },
                       [&](std::monostate _) -> void { ZX_ASSERT(!Ok()); },
                   },
                   after_colon);

        if (!Ok()) {
          return nullptr;
        }
        break;
      }
      default: {
        if (modifiers != nullptr)
          ValidateModifiers</* none */>(modifiers, identifier->start_);
        if (attributes != nullptr)
          Fail(ErrCannotAttachAttributeToIdentifier, attributes->span());
        layout = std::move(identifier);
      }
    }
  }

  // Build a LayoutReference of the right type based on the underlying type of the layout.
  ZX_ASSERT_MSG(std::holds_alternative<std::unique_ptr<raw::CompoundIdentifier>>(layout) ||
                    std::holds_alternative<std::unique_ptr<raw::Layout>>(layout),
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
    if (constraints != nullptr) {
      return Fail(ErrMultipleConstraintDefinitions, previous_token_.span());
    }
    constraints = ParseTypeConstraints();
    if (!Ok())
      return Fail();
  }

  ZX_ASSERT_MSG(layout_ref != nullptr,
                "ParseTypeConstructor must always produce a non-null layout_ref");
  return std::make_unique<raw::TypeConstructor>(scope.GetSourceElement(), std::move(layout_ref),
                                                std::move(parameters), std::move(constraints));
}

std::unique_ptr<raw::TypeDecl> Parser::ParseTypeDecl(std::unique_ptr<raw::AttributeList> attributes,
                                                     ASTScope& scope) {
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kType));
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  ConsumeToken(OfKind(Token::Kind::kEqual));
  if (!Ok())
    return Fail();

  auto layout = ParseTypeConstructor();
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

std::unique_ptr<raw::File> Parser::ParseFile() {
  ASTScope scope(this);

  auto library_decl = ParseLibraryDecl();
  if (!Ok())
    return Fail();
  ConsumeToken(OfKind(Token::Kind::kSemicolon));
  if (!Ok())
    return Fail();

  bool done_with_library_imports = false;
  std::vector<std::unique_ptr<raw::AliasDeclaration>> alias_list;
  std::vector<std::unique_ptr<raw::Using>> using_list;
  std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
  std::vector<std::unique_ptr<raw::ProtocolDeclaration>> protocol_declaration_list;
  std::vector<std::unique_ptr<raw::ResourceDeclaration>> resource_declaration_list;
  std::vector<std::unique_ptr<raw::ServiceDeclaration>> service_declaration_list;
  std::vector<std::unique_ptr<raw::TypeDecl>> type_decls;
  auto parse_declaration = [&]() {
    ASTScope scope(this);
    std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
    if (!Ok())
      return More;

    switch (Peek().combined()) {
      default:
        Fail(ErrExpectedDeclaration, last_token_.data());
        return More;

      case CASE_TOKEN(Token::Kind::kEndOfFile):
        return Done;

      case CASE_IDENTIFIER(Token::Subkind::kAlias): {
        done_with_library_imports = true;
        add(&alias_list, [&] { return ParseAliasDeclaration(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kConst): {
        done_with_library_imports = true;
        add(&const_declaration_list,
            [&] { return ParseConstDeclaration(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kType): {
        done_with_library_imports = true;
        add(&type_decls, [&] { return ParseTypeDecl(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kAjar):
      case CASE_IDENTIFIER(Token::Subkind::kClosed):
      case CASE_IDENTIFIER(Token::Subkind::kOpen):
        if (!experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kUnknownInteractions)) {
          Fail(ErrExpectedDeclaration, last_token_.data());
          return More;
        }
        [[fallthrough]];
      case CASE_IDENTIFIER(Token::Subkind::kProtocol): {
        done_with_library_imports = true;
        add(&protocol_declaration_list,
            [&] { return ParseProtocolDeclaration(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kResourceDefinition): {
        done_with_library_imports = true;
        add(&resource_declaration_list,
            [&] { return ParseResourceDeclaration(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kService): {
        done_with_library_imports = true;
        add(&service_declaration_list,
            [&] { return ParseServiceDeclaration(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kUsing): {
        add(&using_list, [&] { return ParseUsing(std::move(attributes), scope); });
        if (Ok() && done_with_library_imports) {
          Fail(ErrLibraryImportsMustBeGroupedAtTopOfFile, using_list.back()->span());
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
      }
      if (result == RecoverResult::EndOfScope) {
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
      std::move(using_list), std::move(const_declaration_list),
      std::move(protocol_declaration_list), std::move(resource_declaration_list),
      std::move(service_declaration_list), std::move(type_decls), std::move(tokens_));
}

bool Parser::ConsumeTokensUntil(std::set<Token::Kind> exit_tokens) {
  auto p = [&](const Token& token) -> std::unique_ptr<Diagnostic> {
    if (exit_tokens.count(token.kind()) > 0) {
      // signal to ReadToken to stop by returning an error
      return Diagnostic::MakeError(ErrUnexpectedToken, token.span());
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
      Token::Kind::kEndOfFile,
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
