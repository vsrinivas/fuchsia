// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/parser.h"

#include <errno.h>
#include <lib/fit/function.h>

#include <regex>

#include "fidl/attributes.h"
#include "fidl/diagnostics.h"
#include "fidl/experimental_flags.h"
#include "fidl/types.h"

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
  case CASE_IDENTIFIER(Token::Subkind::kHandle): \
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

using types::Strictness;

enum {
  More,
  Done,
};

bool IsValidLibraryComponentName(const std::string& component) {
  static const std::regex kPattern("^[a-z][a-z0-9]*$");
  return std::regex_match(component, kPattern);
}

// IsIdentifierValid disallows identifiers (escaped, and unescaped) from
// starting or ending with underscore.
bool IsIdentifierValid(const std::string& identifier) {
  // this pattern comes from the FIDL language spec, available here:
  // https://fuchsia.googlesource.com/fuchsia/+/HEAD/docs/development/languages/fidl/reference/language.md#identifiers
  static const std::regex kPattern("^[A-Za-z0-9]([A-Za-z0-9_]*[A-Za-z0-9])?$");
  return std::regex_match(identifier, kPattern);
}

template <typename T, typename Fn>
void add(std::vector<std::unique_ptr<T>>* elements, Fn producer_fn) {
  fit::function<std::unique_ptr<T>()> producer(producer_fn);
  auto element = producer();
  if (element)
    elements->emplace_back(std::move(element));
}

}  // namespace

Parser::Parser(Lexer* lexer, Reporter* reporter, ExperimentalFlags experimental_flags)
    : lexer_(lexer), reporter_(reporter), experimental_flags_(experimental_flags) {
  handle_subtype_table_ = {
      {"bti", types::HandleSubtype::kBti},
      {"channel", types::HandleSubtype::kChannel},
      {"clock", types::HandleSubtype::kClock},
      {"debuglog", types::HandleSubtype::kLog},
      {"event", types::HandleSubtype::kEvent},
      {"eventpair", types::HandleSubtype::kEventpair},
      {"exception", types::HandleSubtype::kException},
      {"fifo", types::HandleSubtype::kFifo},
      {"guest", types::HandleSubtype::kGuest},
      {"interrupt", types::HandleSubtype::kInterrupt},
      {"iommu", types::HandleSubtype::kIommu},
      {"job", types::HandleSubtype::kJob},
      {"pager", types::HandleSubtype::kPager},
      {"pcidevice", types::HandleSubtype::kPciDevice},
      {"pmt", types::HandleSubtype::kPmt},
      {"port", types::HandleSubtype::kPort},
      {"process", types::HandleSubtype::kProcess},
      {"profile", types::HandleSubtype::kProfile},
      {"resource", types::HandleSubtype::kResource},
      {"socket", types::HandleSubtype::kSocket},
      {"stream", types::HandleSubtype::kStream},
      {"suspendtoken", types::HandleSubtype::kSuspendToken},
      {"thread", types::HandleSubtype::kThread},
      {"timer", types::HandleSubtype::kTimer},
      {"vcpu", types::HandleSubtype::kVcpu},
      {"vmar", types::HandleSubtype::kVmar},
      {"vmo", types::HandleSubtype::kVmo},
  };

  state_ = State::kNormal;
  last_token_ = Lex();
}

bool Parser::LookupHandleSubtype(const raw::Identifier* identifier,
                                 std::optional<types::HandleSubtype>* out_handle_subtype) {
  auto lookup = handle_subtype_table_.find(identifier->span().data());
  if (lookup == handle_subtype_table_.end()) {
    return false;
  }
  *out_handle_subtype = lookup->second;
  return true;
}

std::nullptr_t Parser::Fail() { return Fail(ErrUnexpectedToken); }

std::nullptr_t Parser::Fail(std::unique_ptr<Diagnostic> err) {
  assert(err && "should not report nullptr error");
  if (Ok()) {
    err->span = last_token_.span();
    reporter_->ReportError(std::move(err));
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
    reporter_->ReportError(err, token, args...);
  }
  return nullptr;
}

template <typename... Args>
std::nullptr_t Parser::Fail(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                            const Args&... args) {
  if (Ok()) {
    reporter_->ReportError(err, span, args...);
  }
  return nullptr;
}

std::optional<types::Strictness> Parser::MaybeParseStrictness() {
  switch (Peek().combined()) {
    case CASE_IDENTIFIER(Token::Subkind::kStrict):
      ConsumeToken(IdentifierOfSubkind(Token::Subkind::kStrict));
      assert(Ok() && "we should have just seen a strict token");
      return std::optional{types::Strictness::kStrict};
    case CASE_IDENTIFIER(Token::Subkind::kFlexible):
      ConsumeToken(IdentifierOfSubkind(Token::Subkind::kFlexible));
      assert(Ok() && "we should have just seen a flexible token");
      return std::optional{types::Strictness::kFlexible};
    default:
      return std::nullopt;
  }
}

std::unique_ptr<raw::Identifier> Parser::ParseIdentifier(bool is_discarded) {
  ASTScope scope(this, is_discarded);
  std::optional<Token> token = ConsumeToken(OfKind(Token::Kind::kIdentifier));
  if (!Ok() || !token)
    return Fail();
  std::string identifier(token->data());
  if (!IsIdentifierValid(identifier))
    return Fail(ErrInvalidIdentifier, identifier);

  return std::make_unique<raw::Identifier>(scope.GetSourceElement());
}

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseCompoundIdentifier() {
  ASTScope scope(this);
  std::vector<std::unique_ptr<raw::Identifier>> components;

  components.emplace_back(ParseIdentifier());
  if (!Ok())
    return Fail();

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

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseLibraryName() {
  auto library_name = ParseCompoundIdentifier();
  if (!Ok())
    return Fail();

  for (const auto& component : library_name->components) {
    std::string component_data(component->start_.data());
    if (!IsValidLibraryComponentName(component_data)) {
      return Fail(ErrInvalidLibraryNameComponent, component->start_, component_data);
    }
  }

  return library_name;
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

std::unique_ptr<raw::Attribute> Parser::ParseAttribute() {
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
  std::string str_value("");
  if (name)
    str_name = std::string(name->span().data().data(), name->span().data().size());
  if (value) {
    auto data = value->span().data();
    if (data.size() >= 2 && data[0] == '"' && data[data.size() - 1] == '"') {
      str_value = std::string(value->span().data().data() + 1, value->span().data().size() - 2);
    }
  }
  return std::make_unique<raw::Attribute>(scope.GetSourceElement(), str_name, str_value);
}

std::unique_ptr<raw::AttributeList> Parser::ParseAttributeList(
    std::unique_ptr<raw::Attribute> doc_comment, ASTScope& scope) {
  AttributesBuilder attributes_builder(reporter_);
  if (doc_comment) {
    if (!attributes_builder.Insert(std::move(*doc_comment.get())))
      return Fail();
  }
  ConsumeToken(OfKind(Token::Kind::kLeftSquare));
  if (!Ok())
    return Fail();
  for (;;) {
    auto attribute = ParseAttribute();
    if (!Ok())
      return Fail();
    if (!attributes_builder.Insert(std::move(*attribute.get())))
      return Fail();
    if (!MaybeConsumeToken(OfKind(Token::Kind::kComma)))
      break;
  }
  ConsumeToken(OfKind(Token::Kind::kRightSquare));
  if (!Ok())
    return Fail();
  auto attribute_list =
      std::make_unique<raw::AttributeList>(scope.GetSourceElement(), attributes_builder.Done());
  return attribute_list;
}

std::unique_ptr<raw::Attribute> Parser::ParseDocComment() {
  ASTScope scope(this);
  std::string str_value("");

  std::optional<Token> doc_line;
  bool is_first_doc_comment = true;
  while (Peek().kind() == Token::Kind::kDocComment) {
    if (is_first_doc_comment) {
      is_first_doc_comment = false;
    } else {
      // disallow any blank lines between this doc comment and the previous one
      std::string_view trailing_whitespace = last_token_.previous_end().data();
      if (std::count(trailing_whitespace.cbegin(), trailing_whitespace.cend(), '\n') > 1)
        reporter_->ReportWarning(WarnBlankLinesWithinDocCommentBlock, previous_token_);
    }

    doc_line = ConsumeToken(OfKind(Token::Kind::kDocComment));
    if (!Ok() || !doc_line)
      return Fail();
    // NOTE: we currently explicitly only support UNIX line endings
    str_value +=
        std::string(doc_line->span().data().data() + 3, doc_line->span().data().size() - 2);
  }

  if (Peek().kind() == Token::Kind::kEndOfFile)
    reporter_->ReportWarning(WarnDocCommentMustBeFollowedByDeclaration, previous_token_);

  return std::make_unique<raw::Attribute>(scope.GetSourceElement(), "Doc", str_value);
}

std::unique_ptr<raw::AttributeList> Parser::MaybeParseAttributeList(bool for_parameter) {
  ASTScope scope(this);
  std::unique_ptr<raw::Attribute> doc_comment;
  // Doc comments must appear above attributes
  if (Peek().kind() == Token::Kind::kDocComment) {
    doc_comment = ParseDocComment();
  }
  if (for_parameter && doc_comment) {
    reporter_->ReportError(ErrDocCommentOnParameters, previous_token_);
    return Fail();
  }
  if (Peek().kind() == Token::Kind::kLeftSquare) {
    return ParseAttributeList(std::move(doc_comment), scope);
  }
  // no generic attributes, start the attribute list
  if (doc_comment) {
    AttributesBuilder attributes_builder(reporter_);
    if (!attributes_builder.Insert(std::move(*doc_comment.get())))
      return Fail();
    return std::make_unique<raw::AttributeList>(scope.GetSourceElement(),
                                                attributes_builder.Done());
  }
  return nullptr;
}

std::unique_ptr<raw::Constant> Parser::ParseConstant() {
  std::unique_ptr<raw::Constant> constant;
  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kIdentifier): {
      auto identifier = ParseCompoundIdentifier();
      if (!Ok())
        return Fail();
      constant = std::make_unique<raw::IdentifierConstant>(std::move(identifier));
      break;
    }

    TOKEN_LITERAL_CASES : {
      auto literal = ParseLiteral();
      if (!Ok())
        return Fail();
      constant = std::make_unique<raw::LiteralConstant>(std::move(literal));
      break;
    }

    case CASE_TOKEN(Token::Kind::kLeftParen): {
      if (!experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kEnableHandleRights))
        return Fail();

      ASTScope scope(this);
      ConsumeToken(OfKind(Token::Kind::kLeftParen));
      constant = ParseConstant();
      ConsumeToken(OfKind(Token::Kind::kRightParen));
      if (!Ok())
        return Fail();
      constant->update_span(scope.GetSourceElement());
      break;
    }

    default:
      return Fail();
  }

  if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kEnableHandleRights) &&
      Peek().combined() == Token::Kind::kPipe) {
    ConsumeToken(OfKind(Token::Kind::kPipe));
    std::unique_ptr right_operand = ParseConstant();
    if (!Ok())
      return Fail();
    return std::make_unique<raw::BinaryOperatorConstant>(
        std::move(constant), std::move(right_operand), raw::BinaryOperatorConstant::Operator::kOr);
  }
  return constant;
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
  std::unique_ptr<raw::TypeConstructor> maybe_type_ctor;

  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kAs))) {
    if (!Ok())
      return Fail();
    maybe_alias = ParseIdentifier();
    if (!Ok())
      return Fail();
  } else if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
    if (!Ok() || using_path->components.size() != 1u)
      return Fail(ErrCompoundAliasIdentifier, using_path->span());
    maybe_type_ctor = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  }

  return std::make_unique<raw::Using>(scope.GetSourceElement(), std::move(attributes),
                                      std::move(using_path), std::move(maybe_alias),
                                      std::move(maybe_type_ctor));
}

std::unique_ptr<raw::TypeConstructor> Parser::ParseTypeConstructor() {
  ASTScope scope(this);
  auto identifier = ParseCompoundIdentifier();
  if (!Ok())
    return Fail();
  std::unique_ptr<raw::TypeConstructor> maybe_arg_type_ctor;
  // Either handle_subtype or handle_subtype_identifier will be set.
  // handle_subtype is the old version with a hardcoded set of handle subtypes,
  // handle_subtype_identifier is used otherwise, which allows deferral of the
  // mapping (which is looked up in the handle resource later). Old handle
  // subtypes are handle<job>, whereas new fidl-defined handle subtypes are
  // constraints, e.g. handle:JOB.
  std::optional<types::HandleSubtype> handle_subtype;
  std::unique_ptr<raw::Constant> handle_rights;
  if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
    if (!Ok())
      return Fail();
    bool is_handle_identifier =
        identifier->components.size() == 1 && identifier->components[0]->span().data() == "handle";
    if (is_handle_identifier) {
      if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kDisallowOldHandleSyntax)) {
        return Fail(ErrOldHandleSyntax);
      }
      auto sub_identifier = ParseIdentifier(true);
      if (!Ok())
        return Fail();
      if (!LookupHandleSubtype(sub_identifier.get(), &handle_subtype))
        return Fail();
      if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kEnableHandleRights)) {
        if (MaybeConsumeToken(OfKind(Token::Kind::kComma))) {
          handle_rights = ParseConstant();
        }
      }
    } else {
      maybe_arg_type_ctor = ParseTypeConstructor();
      if (!Ok())
        return Fail();
    }
    ConsumeToken(OfKind(Token::Kind::kRightAngle));
    if (!Ok())
      return Fail();
  }
  std::unique_ptr<raw::Constant> maybe_size;
  std::unique_ptr<raw::Identifier> handle_subtype_identifier;
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    if (!Ok())
      return Fail();
    if (identifier->components.back()->span().data() == "handle") {
      if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
        handle_subtype_identifier = ParseIdentifier();
        if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kEnableHandleRights)) {
          if (MaybeConsumeToken(OfKind(Token::Kind::kComma))) {
            handle_rights = ParseConstant();
          }
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
  auto nullability = types::Nullability::kNonnullable;
  if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
    if (!Ok())
      return Fail();
    nullability = types::Nullability::kNullable;
  }

  return std::make_unique<raw::TypeConstructor>(
      scope.GetSourceElement(), std::move(identifier), std::move(maybe_arg_type_ctor),
      handle_subtype, std::move(handle_subtype_identifier), std::move(handle_rights),
      std::move(maybe_size), nullability);
}

std::unique_ptr<raw::BitsMember> Parser::ParseBitsMember() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList();
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope, types::Strictness strictness) {
  std::vector<std::unique_ptr<raw::BitsMember>> members;
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kBits));
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::TypeConstructor> maybe_type_ctor;
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    if (!Ok())
      return Fail();
    maybe_type_ctor = ParseTypeConstructor();
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

  return std::make_unique<raw::BitsDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                std::move(identifier), std::move(maybe_type_ctor),
                                                std::move(members), strictness);
}

std::unique_ptr<raw::ConstDeclaration> Parser::ParseConstDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kConst));

  if (!Ok())
    return Fail();
  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
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

std::unique_ptr<raw::EnumMember> Parser::ParseEnumMember() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList();
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope, types::Strictness strictness) {
  std::vector<std::unique_ptr<raw::EnumMember>> members;
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kEnum));
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  std::unique_ptr<raw::TypeConstructor> maybe_type_ctor;
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    if (!Ok())
      return Fail();
    maybe_type_ctor = ParseTypeConstructor();
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

  return std::make_unique<raw::EnumDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                std::move(identifier), std::move(maybe_type_ctor),
                                                std::move(members), strictness);
}

std::unique_ptr<raw::Parameter> Parser::ParseParameter() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList(/*for_parameter=*/true);
  if (!Ok())
    return Fail();
  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::Parameter>(scope.GetSourceElement(), std::move(type_ctor),
                                          std::move(identifier), std::move(attributes));
}

std::unique_ptr<raw::ParameterList> Parser::ParseParameterList() {
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

  return std::make_unique<raw::ParameterList>(scope.GetSourceElement(), std::move(parameter_list));
}

std::unique_ptr<raw::ProtocolMethod> Parser::ParseProtocolEvent(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
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
    if (!Ok())
      return false;

    return true;
  };

  std::unique_ptr<raw::ParameterList> response;
  if (!parse_params(&response))
    return Fail();

  std::unique_ptr<raw::TypeConstructor> maybe_error;
  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kError))) {
    maybe_error = ParseTypeConstructor();
    if (!Ok())
      return Fail();
  }

  assert(method_name);
  assert(response);

  return std::make_unique<raw::ProtocolMethod>(scope.GetSourceElement(), std::move(attributes),
                                               std::move(method_name), nullptr /* maybe_request */,
                                               std::move(response), std::move(maybe_error));
}

std::unique_ptr<raw::ProtocolMethod> Parser::ParseProtocolMethod(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope,
    std::unique_ptr<raw::Identifier> method_name) {
  auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
    *params_out = ParseParameterList();
    if (!Ok())
      return false;
    return true;
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

  assert(method_name);
  assert(request);

  return std::make_unique<raw::ProtocolMethod>(scope.GetSourceElement(), std::move(attributes),
                                               std::move(method_name), std::move(request),
                                               std::move(maybe_response), std::move(maybe_error));
}

void Parser::ParseProtocolMember(
    std::vector<std::unique_ptr<raw::ComposeProtocol>>* composed_protocols,
    std::vector<std::unique_ptr<raw::ProtocolMethod>>* methods) {
  ASTScope scope(this);
  std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
  if (!Ok())
    Fail();

  switch (Peek().kind()) {
    case Token::Kind::kArrow: {
      add(methods, [&] { return ParseProtocolEvent(std::move(attributes), scope); });
      break;
    }
    case Token::Kind::kIdentifier: {
      auto identifier = ParseIdentifier();
      if (!Ok())
        break;
      if (Peek().kind() == Token::Kind::kLeftParen) {
        add(methods, [&] {
          return ParseProtocolMethod(std::move(attributes), scope, std::move(identifier));
        });
        break;
      } else if (identifier->span().data() == "compose") {
        if (attributes) {
          Fail(ErrCannotAttachAttributesToCompose);
          break;
        }
        auto protocol_name = ParseCompoundIdentifier();
        if (!Ok())
          break;
        composed_protocols->push_back(std::make_unique<raw::ComposeProtocol>(
            raw::SourceElement(identifier->start_, protocol_name->end_), std::move(protocol_name)));
        break;
      } else {
        Fail(ErrUnrecognizedProtocolMember);
        break;
      }
    }
    default:
      Fail(ErrExpectedProtocolMember);
      break;
  }
}

std::unique_ptr<raw::ProtocolDeclaration> Parser::ParseProtocolDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
  std::vector<std::unique_ptr<raw::ComposeProtocol>> composed_protocols;
  std::vector<std::unique_ptr<raw::ProtocolMethod>> methods;

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
  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
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
    if (!Ok())
      return Fail();
    maybe_type_ctor = ParseTypeConstructor();
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
  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
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
  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();
  auto type_ctor = ParseTypeConstructor();
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope,
    types::Resourceness resourceness) {
  std::vector<std::unique_ptr<raw::StructMember>> members;

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kStruct));
  if (!Ok())
    return Fail();
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
    Fail();

  return std::make_unique<raw::StructDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                  std::move(identifier), std::move(members),
                                                  resourceness);
}

std::unique_ptr<raw::TableMember> Parser::ParseTableMember() {
  ASTScope scope(this);
  std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
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

  auto type_ctor = ParseTypeConstructor();
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope, types::Strictness strictness,
    types::Resourceness resourceness) {
  std::vector<std::unique_ptr<raw::TableMember>> members;

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kTable));
  if (!Ok())
    return Fail();
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

  return std::make_unique<raw::TableDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                 std::move(identifier), std::move(members),
                                                 strictness, resourceness);
}

std::unique_ptr<raw::UnionMember> Parser::ParseUnionMember() {
  ASTScope scope(this);

  auto attributes = MaybeParseAttributeList();
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

  auto type_ctor = ParseTypeConstructor();
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope, types::Strictness strictness,
    types::Resourceness resourceness) {
  std::vector<std::unique_ptr<raw::UnionMember>> members;

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

  return std::make_unique<raw::UnionDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                 std::move(identifier), std::move(members),
                                                 strictness, resourceness);
}

std::unique_ptr<raw::File> Parser::ParseFile() {
  ASTScope scope(this);
  bool done_with_library_imports = false;
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

  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();
  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kLibrary));
  if (!Ok())
    return Fail();
  auto library_name = ParseLibraryName();
  if (!Ok())
    return Fail();
  ConsumeToken(OfKind(Token::Kind::kSemicolon));
  if (!Ok())
    return Fail();

  auto parse_declaration = [&bits_declaration_list, &const_declaration_list, &enum_declaration_list,
                            &protocol_declaration_list, &resource_declaration_list,
                            &service_declaration_list, &struct_declaration_list,
                            &done_with_library_imports, &using_list, &table_declaration_list,
                            &union_declaration_list, this]() {
    ASTScope scope(this);
    std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
    if (!Ok())
      return More;

    auto maybe_strictness = MaybeParseStrictness();
    if (!Ok())
      return More;

    auto resourceness = types::Resourceness::kResource;
    if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kDefaultNoHandles)) {
      resourceness = types::Resourceness::kValue;
      switch (Peek().combined()) {
        case CASE_IDENTIFIER(Token::Subkind::kResource):
          ConsumeToken(IdentifierOfSubkind(Token::Subkind::kResource));
          assert(Ok() && "we should have just seen a resource token");
          resourceness = types::Resourceness::kResource;
          switch (Peek().combined()) {
            case CASE_TOKEN(Token::Kind::kEndOfFile):
              Fail();
              return Done;
            case CASE_IDENTIFIER(Token::Subkind::kStruct):
            case CASE_IDENTIFIER(Token::Subkind::kTable):
            case CASE_IDENTIFIER(Token::Subkind::kUnion):
            case CASE_IDENTIFIER(Token::Subkind::kXUnion):
              break;
            default:
              // TODO(fxb/57409): Improve this error message.
              Fail(ErrCannotSpecifyResource, Peek());
          }
          break;
      }
    }

    if (maybe_strictness) {
      switch (Peek().combined()) {
        case CASE_IDENTIFIER(Token::Subkind::kBits):
        case CASE_IDENTIFIER(Token::Subkind::kEnum):
        case CASE_IDENTIFIER(Token::Subkind::kUnion):
        case CASE_IDENTIFIER(Token::Subkind::kXUnion):
          break;
        default:
          // TODO(fxb/57409): Improve this error message.
          Fail(ErrCannotSpecifyStrict, Peek());
          return More;
      }
    }

    switch (Peek().combined()) {
      default:
        Fail(ErrExpectedDeclaration);
        return More;

      case CASE_TOKEN(Token::Kind::kEndOfFile):
        return Done;

      case CASE_IDENTIFIER(Token::Subkind::kBits): {
        done_with_library_imports = true;
        add(&bits_declaration_list, [&] {
          return ParseBitsDeclaration(std::move(attributes), scope,
                                      maybe_strictness.value_or(types::Strictness::kStrict));
        });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kConst): {
        done_with_library_imports = true;
        add(&const_declaration_list,
            [&] { return ParseConstDeclaration(std::move(attributes), scope); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kEnum): {
        done_with_library_imports = true;
        add(&enum_declaration_list, [&] {
          return ParseEnumDeclaration(std::move(attributes), scope,
                                      maybe_strictness.value_or(types::Strictness::kStrict));
        });
        return More;
      }

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

      case CASE_IDENTIFIER(Token::Subkind::kStruct): {
        done_with_library_imports = true;
        add(&struct_declaration_list,
            [&] { return ParseStructDeclaration(std::move(attributes), scope, resourceness); });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kTable): {
        done_with_library_imports = true;
        add(&table_declaration_list, [&] {
          return ParseTableDeclaration(std::move(attributes), scope, types::Strictness::kFlexible,
                                       resourceness);
        });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kUsing): {
        auto using_decl = ParseUsing(std::move(attributes), scope);
        if (using_decl == nullptr) {
          // Failed to parse using declaration.
          return Done;
        }
        if (using_decl->maybe_type_ctor) {
          done_with_library_imports = true;
        } else if (done_with_library_imports) {
          reporter_->ReportError(ErrLibraryImportsMustBeGroupedAtTopOfFile, using_decl->span());
        }
        using_list.emplace_back(std::move(using_decl));
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kUnion): {
        done_with_library_imports = true;
        ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUnion));
        add(&union_declaration_list, [&] {
          return ParseUnionDeclaration(std::move(attributes), scope,
                                       maybe_strictness.value_or(types::Strictness::kStrict),
                                       resourceness);
        });
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kXUnion):
        auto strictness = maybe_strictness.value_or(types::Strictness::kFlexible);
        switch (strictness) {
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
      scope.GetSourceElement(), end.value(), std::move(attributes), std::move(library_name),
      std::move(using_list), std::move(bits_declaration_list), std::move(const_declaration_list),
      std::move(enum_declaration_list), std::move(protocol_declaration_list),
      std::move(resource_declaration_list), std::move(service_declaration_list),
      std::move(struct_declaration_list), std::move(table_declaration_list),
      std::move(union_declaration_list));
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

Parser::RecoverResult Parser::RecoverToEndOfDecl() {
  recovered_errors_ = reporter_->errors().size();

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
  recovered_errors_ = reporter_->errors().size();

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

Parser::RecoverResult Parser::RecoverToEndOfParam() {
  recovered_errors_ = reporter_->errors().size();

  static const auto exit_tokens = std::set<Token::Kind>{
      Token::Kind::kComma,      Token::Kind::kRightParen, Token::Kind::kSemicolon,
      Token::Kind::kRightCurly, Token::Kind::kEndOfFile,
  };
  if (!ConsumeTokensUntil(exit_tokens)) {
    return RecoverResult::Failure;
  }

  switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kComma):
      return RecoverResult::Continue;
    case CASE_TOKEN(Token::Kind::kRightParen):
      return RecoverResult::EndOfScope;
    default:
      return RecoverResult::Failure;
  }
}

}  // namespace fidl
