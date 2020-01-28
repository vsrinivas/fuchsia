// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/parser.h"

#include <errno.h>

#include <regex>

#include "fidl/attributes.h"
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

}  // namespace

Parser::Parser(Lexer* lexer, ErrorReporter* error_reporter,
               const ExperimentalFlags& experimental_flags)
    : lexer_(lexer), error_reporter_(error_reporter), experimental_flags_(experimental_flags) {
  handle_subtype_table_ = {
      {"bti", types::HandleSubtype::kBti},
      {"channel", types::HandleSubtype::kChannel},
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

decltype(nullptr) Parser::Fail() { return Fail("found unexpected token"); }

decltype(nullptr) Parser::Fail(std::string_view message) {
  return Fail(last_token_, std::move(message));
}

decltype(nullptr) Parser::Fail(Token token, std::string_view message) {
  if (Ok()) {
    error_reporter_->ReportError(token, std::move(message));
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
  Token token = ConsumeToken(OfKind(Token::Kind::kIdentifier));
  if (!Ok())
    return Fail();
  std::string identifier(token.data());
  if (!IsIdentifierValid(identifier))
    return Fail("invalid identifier '" + identifier + "'");

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
      return Fail(component->start_, "Invalid library name component " + component_data);
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

std::unique_ptr<raw::Ordinal32> Parser::ParseOrdinal32() {
  ASTScope scope(this);

  ConsumeToken(OfKind(Token::Kind::kNumericLiteral));
  if (!Ok())
    return Fail();
  auto data = scope.GetSourceElement().span().data();
  std::string string_data(data.data(), data.data() + data.size());
  errno = 0;
  unsigned long long value = strtoull(string_data.data(), nullptr, 0);
  assert(errno == 0 && "unparsable number should not be lexed.");
  if (value > std::numeric_limits<uint32_t>::max())
    return Fail("ordinal out-of-bound");
  uint32_t ordinal = static_cast<uint32_t>(value);
  if (ordinal == 0u)
    return Fail("ordinals must start at 1");

  ConsumeToken(OfKind(Token::Kind::kColon));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::Ordinal32>(scope.GetSourceElement(), ordinal);
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
  AttributesBuilder attributes_builder(error_reporter_);
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

  Token doc_line;
  bool is_first_doc_comment = true;
  while (Peek().kind() == Token::Kind::kDocComment) {
    if (is_first_doc_comment) {
      is_first_doc_comment = false;
    } else {
      // disallow any blank lines between this doc comment and the previous one
      std::string_view trailing_whitespace = last_token_.previous_end().data();
      if (std::count(trailing_whitespace.cbegin(), trailing_whitespace.cend(), '\n') > 1)
        error_reporter_->ReportWarning(previous_token_,
                                       "cannot have blank lines within doc comment block");
    }

    doc_line = ConsumeToken(OfKind(Token::Kind::kDocComment));
    if (!Ok())
      return Fail();
    // NOTE: we currently explicitly only support UNIX line endings
    str_value += std::string(doc_line.span().data().data() + 3, doc_line.span().data().size() - 2);
  }

  if (Peek().kind() == Token::Kind::kEndOfFile)
    error_reporter_->ReportWarning(previous_token_,
                                   "doc comment must be followed by a declaration");

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
    error_reporter_->ReportError(previous_token_, "cannot have doc comment on parameters");
    return Fail();
  }
  if (Peek().kind() == Token::Kind::kLeftSquare) {
    return ParseAttributeList(std::move(doc_comment), scope);
  }
  // no generic attributes, start the attribute list
  if (doc_comment) {
    AttributesBuilder attributes_builder(error_reporter_);
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

    default:
      return Fail();
  }

  if (experimental_flags_.IsFlagEnabled(ExperimentalFlags::Flag::kEnableHandleRights) &&
      Peek().combined() == Token::Kind::kPipe) {
    ConsumeToken(OfKind(Token::Kind::kPipe));
    std::unique_ptr right_operand = ParseConstant();
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
      return Fail();
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
  std::optional<types::HandleSubtype> handle_subtype;
  std::unique_ptr<raw::Constant> handle_rights;
  if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
    if (!Ok())
      return Fail();
    bool is_handle_identifier =
        identifier->components.size() == 1 && identifier->components[0]->span().data() == "handle";
    if (is_handle_identifier) {
      auto identifier = ParseIdentifier(true);
      if (!Ok())
        return Fail();
      if (!LookupHandleSubtype(identifier.get(), &handle_subtype))
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
  if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
    if (!Ok())
      return Fail();
    maybe_size = ParseConstant();
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
      handle_subtype, std::move(handle_rights), std::move(maybe_size), nullability);
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
      members.emplace_back(ParseBitsMember());
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }
  if (!Ok())
    Fail();

  if (members.empty())
    return Fail("must have at least one bits member");

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
      members.emplace_back(ParseEnumMember());
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }
  if (!Ok())
    Fail();

  if (members.empty())
    return Fail();

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
    if (!Ok())
      return Fail();
    while (Peek().kind() == Token::Kind::kComma) {
      ConsumeToken(OfKind(Token::Kind::kComma));
      if (!Ok())
        return Fail();
      parameter_list.emplace_back(ParseParameter());
      if (!Ok())
        return Fail();
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope,
    std::vector<std::unique_ptr<raw::ComposeProtocol>>* composed_protocols,
    std::vector<std::unique_ptr<raw::ProtocolMethod>>* methods) {
  switch (Peek().kind()) {
    case Token::Kind::kArrow: {
      auto event = ParseProtocolEvent(std::move(attributes), scope);
      methods->push_back(std::move(event));
      break;
    }
    case Token::Kind::kIdentifier: {
      auto identifier = ParseIdentifier();
      if (!Ok())
        break;
      if (Peek().kind() == Token::Kind::kLeftParen) {
        auto method = ParseProtocolMethod(std::move(attributes), scope, std::move(identifier));
        methods->push_back(std::move(method));
        break;
      } else if (identifier->span().data() == "compose") {
        if (attributes) {
          Fail("Cannot attach attributes to compose stanza");
          break;
        }
        auto protocol_name = ParseCompoundIdentifier();
        if (!Ok())
          break;
        composed_protocols->push_back(std::make_unique<raw::ComposeProtocol>(
            raw::SourceElement(identifier->start_, protocol_name->end_), std::move(protocol_name)));
        break;
      } else {
        Fail("unrecognized protocol member");
        break;
      }
    }
    default:
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
    ASTScope scope(this);
    std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
    if (!Ok())
      return More;

    switch (Peek().kind()) {
      default:
        ConsumeToken(OfKind(Token::Kind::kRightCurly));
        return Done;

      case Token::Kind::kArrow:
      case Token::Kind::kIdentifier:
        ParseProtocolMember(std::move(attributes), scope, &composed_protocols, &methods);
        return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }
  if (!Ok())
    Fail();

  return std::make_unique<raw::ProtocolDeclaration>(
      scope.GetSourceElement(), std::move(attributes), std::move(identifier),
      std::move(composed_protocols), std::move(methods));
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
      members.emplace_back(ParseServiceMember());
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
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
      members.emplace_back(ParseStructMember());
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }
  if (!Ok())
    Fail();

  return std::make_unique<raw::StructDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                  std::move(identifier), std::move(members));
}

std::unique_ptr<raw::TableMember> Parser::ParseTableMember() {
  ASTScope scope(this);
  std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();

  auto ordinal = ParseOrdinal32();
  if (!Ok())
    return Fail();

  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kReserved))) {
    if (!Ok())
      return Fail();
    if (attributes != nullptr)
      return Fail("Cannot attach attributes to reserved ordinals");
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
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope, types::Strictness strictness) {
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
      TOKEN_ATTR_CASES:
        members.emplace_back(ParseTableMember());
        return More;

      default:
        std::string msg = "Expected one of ordinal or '}', found ";
        msg.append(Token::Name(Peek()));
        Fail(msg);
        return Done;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }
  if (!Ok())
    Fail();

  return std::make_unique<raw::TableDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                 std::move(identifier), std::move(members),
                                                 strictness);
}

std::unique_ptr<raw::UnionMember> Parser::ParseUnionMember() {
  ASTScope scope(this);
  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();
  auto ordinal = ParseOrdinal32();
  if (!Ok())
    return Fail();

  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kReserved))) {
    if (!Ok())
      return Fail();
    if (attributes)
      return Fail("Cannot attach attributes to reserved ordinals");
    return std::make_unique<raw::UnionMember>(scope.GetSourceElement(), std::move(ordinal));
  }

  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::UnionMember>(scope.GetSourceElement(), std::move(ordinal),
                                            std::move(type_ctor), std::move(identifier),
                                            std::move(attributes));
}

std::unique_ptr<raw::UnionDeclaration> Parser::ParseUnionDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope, types::Strictness strictness) {
  std::vector<std::unique_ptr<raw::UnionMember>> members;

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUnion));
  if (!Ok())
    return Fail();
  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();
  ConsumeToken(OfKind(Token::Kind::kLeftCurly));
  if (!Ok())
    return Fail();

  bool contains_non_reserved_member = false;
  auto parse_member = [&members, &contains_non_reserved_member, this]() {
    if (Peek().kind() == Token::Kind::kRightCurly) {
      ConsumeToken(OfKind(Token::Kind::kRightCurly));
      return Done;
    } else {
      members.emplace_back(ParseUnionMember());
      if (members.back() && members.back()->maybe_used)
        contains_non_reserved_member = true;
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }
  if (!Ok())
    Fail();

  if (!contains_non_reserved_member)
    return Fail(
        "must have at least one non reserved member; you can use an empty struct "
        "to define a placeholder variant");

  return std::make_unique<raw::UnionDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                 std::move(identifier), std::move(members),
                                                 strictness);
}

std::unique_ptr<raw::XUnionMember> Parser::ParseXUnionMember() {
  ASTScope scope(this);

  auto attributes = MaybeParseAttributeList();
  if (!Ok())
    return Fail();
  auto ordinal = ParseOrdinal32();
  if (!Ok())
    return Fail();

  if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kReserved))) {
    if (!Ok())
      return Fail();
    if (attributes)
      return Fail("Cannot attach attributes to reserved ordinals");
    return std::make_unique<raw::XUnionMember>(scope.GetSourceElement(), std::move(ordinal));
  }

  auto type_ctor = ParseTypeConstructor();
  if (!Ok())
    return Fail();

  auto identifier = ParseIdentifier();
  if (!Ok())
    return Fail();

  return std::make_unique<raw::XUnionMember>(scope.GetSourceElement(), std::move(ordinal),
                                             std::move(type_ctor), std::move(identifier),
                                             std::move(attributes));
}

std::unique_ptr<raw::XUnionDeclaration> Parser::ParseXUnionDeclaration(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope, types::Strictness strictness) {
  std::vector<std::unique_ptr<raw::XUnionMember>> members;

  ConsumeToken(IdentifierOfSubkind(Token::Subkind::kXUnion));
  if (!Ok())
    return Fail();

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
      members.emplace_back(ParseXUnionMember());
      if (members.back() && members.back()->maybe_used)
        contains_non_reserved_member = true;
      return More;
    }
  };

  while (parse_member() == More) {
    if (!Ok())
      Fail();

    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }
  if (!Ok())
    return Fail();

  if (!contains_non_reserved_member)
    return Fail(
        "must have at least one non reserved member; you can use an empty struct "
        "to define a placeholder variant");

  return std::make_unique<raw::XUnionDeclaration>(scope.GetSourceElement(), std::move(attributes),
                                                  std::move(identifier), std::move(members),
                                                  strictness);
}

std::unique_ptr<raw::File> Parser::ParseFile() {
  ASTScope scope(this);
  bool done_with_library_imports = false;
  std::vector<std::unique_ptr<raw::Using>> using_list;
  std::vector<std::unique_ptr<raw::BitsDeclaration>> bits_declaration_list;
  std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
  std::vector<std::unique_ptr<raw::EnumDeclaration>> enum_declaration_list;
  std::vector<std::unique_ptr<raw::ProtocolDeclaration>> protocol_declaration_list;
  std::vector<std::unique_ptr<raw::ServiceDeclaration>> service_declaration_list;
  std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;
  std::vector<std::unique_ptr<raw::TableDeclaration>> table_declaration_list;
  std::vector<std::unique_ptr<raw::UnionDeclaration>> union_declaration_list;
  std::vector<std::unique_ptr<raw::XUnionDeclaration>> xunion_declaration_list;

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
                            &protocol_declaration_list, &service_declaration_list,
                            &struct_declaration_list, &done_with_library_imports, &using_list,
                            &table_declaration_list, &union_declaration_list,
                            &xunion_declaration_list, this]() {
    ASTScope scope(this);
    std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
    if (!Ok())
      return More;

    auto maybe_strictness = MaybeParseStrictness();
    if (!Ok())
      return More;

    if (maybe_strictness) {
      switch (Peek().combined()) {
        case CASE_IDENTIFIER(Token::Subkind::kBits):
          [[fallthrough]];
        case CASE_IDENTIFIER(Token::Subkind::kEnum):
          if (maybe_strictness.value() == types::Strictness::kStrict) {
            Fail(previous_token_,
                 "enums and bits are strict by default, please remove the \"strict\" qualifier");
            return More;
          }
          break;
        case CASE_IDENTIFIER(Token::Subkind::kUnion):
          break;
        case CASE_IDENTIFIER(Token::Subkind::kXUnion):
          if (maybe_strictness.value() == types::Strictness::kFlexible) {
            Fail(previous_token_,
                 "xunions are flexible by default, please remove the \"flexible\" qualifier");
            return More;
          }
          break;
        default:
          std::string msg = "cannot specify strictness for " + std::string(Token::Name(Peek()));
          Fail(msg);
          return More;
      }
    }

    switch (Peek().combined()) {
      default:
        return Done;

      case CASE_IDENTIFIER(Token::Subkind::kBits):
        done_with_library_imports = true;
        bits_declaration_list.emplace_back(ParseBitsDeclaration(
            std::move(attributes), scope, maybe_strictness.value_or(types::Strictness::kStrict)));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kConst):
        done_with_library_imports = true;
        const_declaration_list.emplace_back(ParseConstDeclaration(std::move(attributes), scope));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kEnum):
        done_with_library_imports = true;
        enum_declaration_list.emplace_back(ParseEnumDeclaration(
            std::move(attributes), scope, maybe_strictness.value_or(types::Strictness::kStrict)));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kProtocol):
        done_with_library_imports = true;
        protocol_declaration_list.emplace_back(
            ParseProtocolDeclaration(std::move(attributes), scope));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kService):
        done_with_library_imports = true;
        service_declaration_list.emplace_back(
            ParseServiceDeclaration(std::move(attributes), scope));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kStruct):
        done_with_library_imports = true;
        struct_declaration_list.emplace_back(ParseStructDeclaration(std::move(attributes), scope));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kTable):
        done_with_library_imports = true;
        table_declaration_list.emplace_back(
            ParseTableDeclaration(std::move(attributes), scope, types::Strictness::kFlexible));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kUsing): {
        auto using_decl = ParseUsing(std::move(attributes), scope);
        if (using_decl->maybe_type_ctor) {
          done_with_library_imports = true;
        } else if (done_with_library_imports) {
          // TODO(FIDL-582): Give one week warning, then turn this into
          // an error.
          error_reporter_->ReportWarning(using_decl->span(),
                                         "library imports must be grouped at top-of-file");
        }
        using_list.emplace_back(std::move(using_decl));
        return More;
      }

      case CASE_IDENTIFIER(Token::Subkind::kUnion):
        done_with_library_imports = true;
        union_declaration_list.emplace_back(ParseUnionDeclaration(
            std::move(attributes), scope, maybe_strictness.value_or(types::Strictness::kStrict)));
        return More;

      case CASE_IDENTIFIER(Token::Subkind::kXUnion):
        done_with_library_imports = true;
        xunion_declaration_list.emplace_back(ParseXUnionDeclaration(
            std::move(attributes), scope, maybe_strictness.value_or(types::Strictness::kFlexible)));
        return More;
    }
  };

  while (parse_declaration() == More) {
    if (!Ok())
      return Fail();
    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
      return Fail();
  }

  Token end = ConsumeToken(OfKind(Token::Kind::kEndOfFile));
  if (!Ok())
    return Fail();

  return std::make_unique<raw::File>(
      scope.GetSourceElement(), end, std::move(attributes), std::move(library_name),
      std::move(using_list), std::move(bits_declaration_list), std::move(const_declaration_list),
      std::move(enum_declaration_list), std::move(protocol_declaration_list),
      std::move(service_declaration_list), std::move(struct_declaration_list),
      std::move(table_declaration_list), std::move(union_declaration_list),
      std::move(xunion_declaration_list));
}

}  // namespace fidl
