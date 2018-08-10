// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/parser.h"

namespace fidl {

#define TOKEN_PRIMITIVE_TYPE_CASES \
    case Token::Kind::kBool:       \
    case Token::Kind::kInt8:       \
    case Token::Kind::kInt16:      \
    case Token::Kind::kInt32:      \
    case Token::Kind::kInt64:      \
    case Token::Kind::kUint8:      \
    case Token::Kind::kUint16:     \
    case Token::Kind::kUint32:     \
    case Token::Kind::kUint64:     \
    case Token::Kind::kFloat32:    \
    case Token::Kind::kFloat64

#define TOKEN_TYPE_CASES           \
    TOKEN_PRIMITIVE_TYPE_CASES:    \
    case Token::Kind::kIdentifier: \
    case Token::Kind::kArray:      \
    case Token::Kind::kVector:     \
    case Token::Kind::kString:     \
    case Token::Kind::kHandle:     \
    case Token::Kind::kRequest

#define TOKEN_LITERAL_CASES            \
    case Token::Kind::kTrue:           \
    case Token::Kind::kFalse:          \
    case Token::Kind::kNumericLiteral: \
    case Token::Kind::kStringLiteral

namespace {
enum {
    More,
    Done,
};
} // namespace

Parser::Parser(Lexer* lexer, ErrorReporter* error_reporter)
    : lexer_(lexer), error_reporter_(error_reporter), latest_discarded_end_() {
    handle_subtype_table_ = {
        {"process", types::HandleSubtype::kProcess},
        {"thread", types::HandleSubtype::kThread},
        {"vmo", types::HandleSubtype::kVmo},
        {"channel", types::HandleSubtype::kChannel},
        {"event", types::HandleSubtype::kEvent},
        {"port", types::HandleSubtype::kPort},
        {"interrupt", types::HandleSubtype::kInterrupt},
        {"log", types::HandleSubtype::kLog},
        {"socket", types::HandleSubtype::kSocket},
        {"resource", types::HandleSubtype::kResource},
        {"eventpair", types::HandleSubtype::kEventpair},
        {"job", types::HandleSubtype::kJob},
        {"vmar", types::HandleSubtype::kVmar},
        {"fifo", types::HandleSubtype::kFifo},
        {"guest", types::HandleSubtype::kGuest},
        {"timer", types::HandleSubtype::kTimer},
    };

    last_token_ = Lex();
}

bool Parser::LookupHandleSubtype(const raw::Identifier* identifier,
                                 types::HandleSubtype* subtype_out) {
    auto lookup = handle_subtype_table_.find(identifier->location().data());
    if (lookup == handle_subtype_table_.end()) {
        return false;
    }
    *subtype_out = lookup->second;
    return true;
}

decltype(nullptr) Parser::Fail() {
    if (ok_) {
        auto token_location = last_token_.location();
        auto token_data = token_location.data();

        SourceFile::Position position;
        std::string surrounding_line = token_location.SourceLine(&position);
        auto line_number = std::to_string(position.line);
        auto column_number = std::to_string(position.column);

        std::string squiggle(position.column, ' ');
        squiggle += "^";
        size_t squiggle_size = token_data.size();
        if (squiggle_size != 0u) {
            --squiggle_size;
        }
        squiggle += std::string(squiggle_size, '~');
        // Some tokens (like string literals) can span multiple
        // lines. Truncate the string to just one line at most. The
        // containing line contains a newline, so drop it when
        // comparing sizes.
        size_t line_size = surrounding_line.size() - 1;
        if (squiggle.size() > line_size) {
            squiggle.resize(line_size);
        }

        std::string error = "found unexpected token in file ";
        error += token_location.source_file().filename();
        error += " on line " + line_number;
        error += " column " + column_number + ":\n";
        error += surrounding_line;
        error += squiggle + "\n";

        error_reporter_->ReportError(std::move(error));
        ok_ = false;
    }
    return nullptr;
}

std::unique_ptr<raw::Identifier> Parser::ParseIdentifier(bool is_discarded) {
    Token identifier = ConsumeToken(Token::Kind::kIdentifier, is_discarded);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Identifier>(identifier, identifier);
}

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseCompoundIdentifier() {
    std::vector<std::unique_ptr<raw::Identifier>> components;

    components.emplace_back(ParseIdentifier());
    Token first_token = components[0]->start_;
    if (!Ok())
        return Fail();

    auto parse_component = [&components, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::kDot:
            ConsumeToken(Token::Kind::kDot, true);
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

    return std::make_unique<raw::CompoundIdentifier>(first_token, MarkLastUseful(), std::move(components));
}

std::unique_ptr<raw::StringLiteral> Parser::ParseStringLiteral() {
    Token string_literal = ConsumeToken(Token::Kind::kStringLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::StringLiteral>(string_literal);
}

std::unique_ptr<raw::NumericLiteral> Parser::ParseNumericLiteral() {
    auto numeric_literal = ConsumeToken(Token::Kind::kNumericLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::NumericLiteral>(numeric_literal);
}

std::unique_ptr<raw::TrueLiteral> Parser::ParseTrueLiteral() {
    Token token = ConsumeToken(Token::Kind::kTrue);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::TrueLiteral>(token);
}

std::unique_ptr<raw::FalseLiteral> Parser::ParseFalseLiteral() {
    Token token = ConsumeToken(Token::Kind::kFalse);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::FalseLiteral>(token);
}

std::unique_ptr<raw::Literal> Parser::ParseLiteral() {
    switch (Peek()) {
    case Token::Kind::kStringLiteral:
        return ParseStringLiteral();

    case Token::Kind::kNumericLiteral:
        return ParseNumericLiteral();

    case Token::Kind::kTrue:
        return ParseTrueLiteral();

    case Token::Kind::kFalse:
        return ParseFalseLiteral();

    default:
        return Fail();
    }
}

std::unique_ptr<raw::Attribute> Parser::ParseAttribute() {
    auto name = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<raw::StringLiteral> value;
    if (MaybeConsumeToken(Token::Kind::kEqual)) {
        value = ParseStringLiteral();
        if (!Ok())
            return Fail();
    }
    return std::make_unique<raw::Attribute>(name->start_, MarkLastUseful(), std::move(name), std::move(value));
}

std::unique_ptr<raw::AttributeList> Parser::ParseAttributeList() {
    Token start = ConsumeToken(Token::Kind::kLeftSquare);
    if (!Ok())
        return Fail();
    std::vector<std::unique_ptr<raw::Attribute>> attribute_list;
    for (;;) {
        attribute_list.emplace_back(ParseAttribute());
        if (!Ok())
            return Fail();
        if (!MaybeConsumeToken(Token::Kind::kComma))
            break;
    }
    ConsumeToken(Token::Kind::kRightSquare, true);
    if (!Ok())
        return Fail();
    return std::make_unique<raw::AttributeList>(start, MarkLastUseful(), std::move(attribute_list));
}

std::unique_ptr<raw::AttributeList> Parser::MaybeParseAttributeList() {
    if (Peek() == Token::Kind::kLeftSquare)
        return ParseAttributeList();
    return nullptr;
}

std::unique_ptr<raw::Constant> Parser::ParseConstant() {
    switch (Peek()) {
    case Token::Kind::kIdentifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        return std::make_unique<raw::IdentifierConstant>(std::move(identifier));
    }

    TOKEN_LITERAL_CASES : {
        auto literal = ParseLiteral();
        if (!Ok())
            return Fail();
        return std::make_unique<raw::LiteralConstant>(std::move(literal));
    }

    default:
        return Fail();
    }
}

std::unique_ptr<raw::Using> Parser::ParseUsing() {
    Token start = ConsumeToken(Token::Kind::kUsing);
    if (!Ok())
        return Fail();
    auto using_path = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Identifier> maybe_alias;
    std::unique_ptr<raw::PrimitiveType> maybe_primitive;

    if (MaybeConsumeToken(Token::Kind::kAs)) {
        if (!Ok())
            return Fail();
        maybe_alias = ParseIdentifier();
        if (!Ok())
            return Fail();
    } else if (MaybeConsumeToken(Token::Kind::kEqual)) {
        if (!Ok() || using_path->components.size() != 1u)
            return Fail();
        maybe_primitive = ParsePrimitiveType();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::Using>(start, MarkLastUseful(), std::move(using_path), std::move(maybe_alias), std::move(maybe_primitive));
}

std::unique_ptr<raw::ArrayType> Parser::ParseArrayType() {
    Token start = ConsumeToken(Token::Kind::kArray);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftAngle, true);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kRightAngle, true);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kColon, true);
    if (!Ok())
        return Fail();
    auto element_count = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ArrayType>(start, MarkLastUseful(), std::move(element_type), std::move(element_count));
}

std::unique_ptr<raw::VectorType> Parser::ParseVectorType() {
    Token start = ConsumeToken(Token::Kind::kVector);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftAngle, true);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kRightAngle, true);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::kColon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::kQuestion)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::VectorType>(start, MarkLastUseful(), std::move(element_type),
                                             std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::StringType> Parser::ParseStringType() {
    Token start = ConsumeToken(Token::Kind::kString);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::kColon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::kQuestion)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::StringType>(start, MarkLastUseful(), std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::HandleType> Parser::ParseHandleType() {
    Token start = ConsumeToken(Token::Kind::kHandle);
    if (!Ok())
        return Fail();

    auto subtype = types::HandleSubtype::kHandle;
    if (MaybeConsumeToken(Token::Kind::kLeftAngle)) {
        if (!Ok())
            return Fail();
        auto identifier = ParseIdentifier(true);
        if (!Ok())
            return Fail();
        if (!LookupHandleSubtype(identifier.get(), &subtype))
            return Fail();
        ConsumeToken(Token::Kind::kRightAngle, true);
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::kQuestion)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::HandleType>(start, MarkLastUseful(), subtype, nullability);
}

std::unique_ptr<raw::PrimitiveType> Parser::ParsePrimitiveType() {
    types::PrimitiveSubtype subtype;

    switch (Peek()) {
    case Token::Kind::kBool:
        subtype = types::PrimitiveSubtype::kBool;
        break;
    case Token::Kind::kInt8:
        subtype = types::PrimitiveSubtype::kInt8;
        break;
    case Token::Kind::kInt16:
        subtype = types::PrimitiveSubtype::kInt16;
        break;
    case Token::Kind::kInt32:
        subtype = types::PrimitiveSubtype::kInt32;
        break;
    case Token::Kind::kInt64:
        subtype = types::PrimitiveSubtype::kInt64;
        break;
    case Token::Kind::kUint8:
        subtype = types::PrimitiveSubtype::kUint8;
        break;
    case Token::Kind::kUint16:
        subtype = types::PrimitiveSubtype::kUint16;
        break;
    case Token::Kind::kUint32:
        subtype = types::PrimitiveSubtype::kUint32;
        break;
    case Token::Kind::kUint64:
        subtype = types::PrimitiveSubtype::kUint64;
        break;
    case Token::Kind::kFloat32:
        subtype = types::PrimitiveSubtype::kFloat32;
        break;
    case Token::Kind::kFloat64:
        subtype = types::PrimitiveSubtype::kFloat64;
        break;
    default:
        return Fail();
    }

    Token start = ConsumeToken(Peek());
    if (!Ok())
        return Fail();
    return std::make_unique<raw::PrimitiveType>(start, MarkLastUseful(), subtype);
}

std::unique_ptr<raw::RequestHandleType> Parser::ParseRequestHandleType() {
    Token start = ConsumeToken(Token::Kind::kRequest);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftAngle, true);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kRightAngle, true);
    if (!Ok())
        return Fail();

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::kQuestion)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::RequestHandleType>(start, MarkLastUseful(), std::move(identifier), nullability);
}

std::unique_ptr<raw::Type> Parser::ParseType() {
    switch (Peek()) {
    case Token::Kind::kIdentifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        auto nullability = types::Nullability::kNonnullable;
        if (MaybeConsumeToken(Token::Kind::kQuestion)) {
            if (!Ok())
                return Fail();
            nullability = types::Nullability::kNullable;
        }
        return std::make_unique<raw::IdentifierType>(identifier->start_, MarkLastUseful(), std::move(identifier), nullability);
    }

    case Token::Kind::kArray: {
        auto type = ParseArrayType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::kVector: {
        auto type = ParseVectorType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::kString: {
        auto type = ParseStringType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::kHandle: {
        auto type = ParseHandleType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::kRequest: {
        auto type = ParseRequestHandleType();
        if (!Ok())
            return Fail();
        return type;
    }

    TOKEN_PRIMITIVE_TYPE_CASES : {
        auto type = ParsePrimitiveType();
        if (!Ok())
            return Fail();
        return type;
    }

    default:
        return Fail();
    }
}

std::unique_ptr<raw::ConstDeclaration>
Parser::ParseConstDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    Token start = ConsumeTokenReturnEarliest(Token::Kind::kConst, attributes);

    if (!Ok())
        return Fail();
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kEqual, true);
    if (!Ok())
        return Fail();
    auto constant = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ConstDeclaration>(start, MarkLastUseful(), std::move(attributes), std::move(type),
                                                   std::move(identifier), std::move(constant));
}

std::unique_ptr<raw::EnumMember> Parser::ParseEnumMember() {
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(Token::Kind::kEqual, true);
    if (!Ok())
        return Fail();

    auto member_value = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::EnumMember>(MarkLastUseful(), std::move(identifier), std::move(member_value));
}

std::unique_ptr<raw::EnumDeclaration>
Parser::ParseEnumDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::EnumMember>> members;

    Token start = ConsumeTokenReturnEarliest(Token::Kind::kEnum, attributes);

    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<raw::PrimitiveType> subtype;
    if (MaybeConsumeToken(Token::Kind::kColon)) {
        if (!Ok())
            return Fail();
        subtype = ParsePrimitiveType();
        if (!Ok())
            return Fail();
    }
    ConsumeToken(Token::Kind::kLeftCurly, true);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly, true);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseEnumMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon, true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail();

    return std::make_unique<raw::EnumDeclaration>(start, MarkLastUseful(),
                                                  std::move(attributes), std::move(identifier),
                                                  std::move(subtype), std::move(members));
}

std::unique_ptr<raw::Parameter> Parser::ParseParameter() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Parameter>(type->start_, MarkLastUseful(), std::move(type), std::move(identifier));
}

std::unique_ptr<raw::ParameterList> Parser::ParseParameterList() {
    std::vector<std::unique_ptr<raw::Parameter>> parameter_list;
    Token start;

    switch (Peek()) {
    default:
        break;

    TOKEN_TYPE_CASES:
        auto parameter = ParseParameter();
        if (start.kind() != Token::Kind::kNotAToken) {
            start = parameter->start_;
        }
        parameter_list.emplace_back(std::move(parameter));
        if (!Ok())
            return Fail();
        while (Peek() == Token::Kind::kComma) {
            ConsumeToken(Token::Kind::kComma, true);
            if (!Ok())
                return Fail();
            switch (Peek()) {
            TOKEN_TYPE_CASES:
                parameter_list.emplace_back(ParseParameter());
                if (!Ok())
                    return Fail();
                break;

            default:
                return Fail();
            }
        }
    }

    return std::make_unique<raw::ParameterList>(start, MarkLastUseful(), std::move(parameter_list));
}

std::unique_ptr<raw::InterfaceMethod> Parser::ParseInterfaceMethod(std::unique_ptr<raw::AttributeList> attributes) {
    Token start;
    auto ordinal = ParseNumericLiteral();
    if (attributes != nullptr && attributes->attribute_list.size() != 0) {
        start = attributes->start_;
    } else {
        start = ordinal->start_;
    }
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kColon, true);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Identifier> method_name;
    std::unique_ptr<raw::ParameterList> maybe_request;
    std::unique_ptr<raw::ParameterList> maybe_response;

    auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
        ConsumeToken(Token::Kind::kLeftParen, true);
        if (!Ok())
            return false;
        *params_out = ParseParameterList();
        if (!Ok())
            return false;
        ConsumeToken(Token::Kind::kRightParen, true);
        if (!Ok())
            return false;
        return true;
    };

    if (MaybeConsumeToken(Token::Kind::kArrow)) {
        method_name = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!parse_params(&maybe_response))
            return Fail();
    } else {
        method_name = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!parse_params(&maybe_request))
            return Fail();

        if (MaybeConsumeToken(Token::Kind::kArrow)) {
            if (!Ok())
                return Fail();
            if (!parse_params(&maybe_response))
                return Fail();
        }
    }

    assert(method_name);
    assert(maybe_request || maybe_response);

    return std::make_unique<raw::InterfaceMethod>(start, MarkLastUseful(),
                                                  std::move(attributes),
                                                  std::move(ordinal),
                                                  std::move(method_name),
                                                  std::move(maybe_request),
                                                  std::move(maybe_response));
}

std::unique_ptr<raw::InterfaceDeclaration>
Parser::ParseInterfaceDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<raw::InterfaceMethod>> methods;

    // The first token may be the word "interface", or it may be the beginning
    // of the attribute list.
    Token start = ConsumeTokenReturnEarliest(Token::Kind::kInterface, attributes);

    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    if (MaybeConsumeToken(Token::Kind::kColon)) {
        for (;;) {
            superinterfaces.emplace_back(ParseCompoundIdentifier());
            if (!Ok())
                return Fail();
            if (!MaybeConsumeToken(Token::Kind::kComma))
                break;
        }
    }

    ConsumeToken(Token::Kind::kLeftCurly, true);
    if (!Ok())
        return Fail();

    auto parse_member = [&methods, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly, true);
            return Done;

        case Token::Kind::kNumericLiteral:
            methods.emplace_back(ParseInterfaceMethod(std::move(attributes)));
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon, true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<raw::InterfaceDeclaration>(start, MarkLastUseful(),
                                                       std::move(attributes), std::move(identifier),
                                                       std::move(superinterfaces),
                                                       std::move(methods));
}

std::unique_ptr<raw::StructMember> Parser::ParseStructMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_default_value;
    if (MaybeConsumeToken(Token::Kind::kEqual)) {
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::StructMember>(MarkLastUseful(),
                                               std::move(type), std::move(identifier),
                                               std::move(maybe_default_value));
}

std::unique_ptr<raw::StructDeclaration>
Parser::ParseStructDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::StructMember>> members;

    Token start = ConsumeTokenReturnEarliest(Token::Kind::kStruct, attributes);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftCurly, true);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly, true);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseStructMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon, true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail();

    return std::make_unique<raw::StructDeclaration>(start, MarkLastUseful(),
                                                    std::move(attributes), std::move(identifier),
                                                    std::move(members));
}

std::unique_ptr<raw::UnionMember> Parser::ParseUnionMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::UnionMember>(type->start_, MarkLastUseful(), std::move(type), std::move(identifier));
}

std::unique_ptr<raw::UnionDeclaration>
Parser::ParseUnionDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::UnionMember>> members;

    Token start = ConsumeTokenReturnEarliest(Token::Kind::kUnion, attributes);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftCurly, true);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly, true);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseUnionMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon, true);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        Fail();

    return std::make_unique<raw::UnionDeclaration>(start, MarkLastUseful(),
                                                   std::move(attributes), std::move(identifier),
                                                   std::move(members));
}

std::unique_ptr<raw::File> Parser::ParseFile() {
    std::vector<std::unique_ptr<raw::Using>> using_list;
    std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<raw::EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<raw::InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<raw::UnionDeclaration>> union_declaration_list;

    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    Token start = ConsumeToken(Token::Kind::kLibrary);
    if (!Ok())
        return Fail();
    auto library_name = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kSemicolon, true);
    if (!Ok())
        return Fail();

    auto parse_using = [&using_list, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::kUsing:
            using_list.emplace_back(ParseUsing());
            return More;
        }
    };

    while (parse_using() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::kSemicolon, true);
        if (!Ok())
            return Fail();
    }

    auto parse_declaration = [&const_declaration_list, &enum_declaration_list,
                              &interface_declaration_list, &struct_declaration_list,
                              &union_declaration_list, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::kConst:
            const_declaration_list.emplace_back(ParseConstDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::kEnum:
            enum_declaration_list.emplace_back(ParseEnumDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::kInterface:
            interface_declaration_list.emplace_back(
                ParseInterfaceDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::kStruct:
            struct_declaration_list.emplace_back(ParseStructDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::kUnion:
            union_declaration_list.emplace_back(ParseUnionDeclaration(std::move(attributes)));
            return More;
        }
    };

    while (parse_declaration() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::kSemicolon, true);
        if (!Ok())
            return Fail();
    }

    Token end = ConsumeToken(Token::Kind::kEndOfFile, false);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::File>(
        start, end,
        std::move(attributes), std::move(library_name), std::move(using_list), std::move(const_declaration_list),
        std::move(enum_declaration_list), std::move(interface_declaration_list),
        std::move(struct_declaration_list), std::move(union_declaration_list));
}

} // namespace fidl
