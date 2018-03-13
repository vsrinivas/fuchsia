// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/parser.h"

namespace fidl {

#define TOKEN_PRIMITIVE_TYPE_CASES                                                                 \
    case Token::Kind::Bool:                                                                        \
    case Token::Kind::Status:                                                                      \
    case Token::Kind::Int8:                                                                        \
    case Token::Kind::Int16:                                                                       \
    case Token::Kind::Int32:                                                                       \
    case Token::Kind::Int64:                                                                       \
    case Token::Kind::Uint8:                                                                       \
    case Token::Kind::Uint16:                                                                      \
    case Token::Kind::Uint32:                                                                      \
    case Token::Kind::Uint64:                                                                      \
    case Token::Kind::Float32:                                                                     \
    case Token::Kind::Float64

#define TOKEN_TYPE_CASES                                                                           \
    TOKEN_PRIMITIVE_TYPE_CASES:                                                                    \
    case Token::Kind::Identifier:                                                                  \
    case Token::Kind::Array:                                                                       \
    case Token::Kind::Vector:                                                                      \
    case Token::Kind::String:                                                                      \
    case Token::Kind::Handle:                                                                      \
    case Token::Kind::Request

#define TOKEN_LITERAL_CASES                                                                        \
    case Token::Kind::True:                                                                        \
    case Token::Kind::False:                                                                       \
    case Token::Kind::NumericLiteral:                                                              \
    case Token::Kind::StringLiteral

namespace {
enum {
    More,
    Done,
};
} // namespace

Parser::Parser(Lexer* lexer, ErrorReporter* error_reporter)
    : lexer_(lexer), error_reporter_(error_reporter) {
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
    auto lookup = handle_subtype_table_.find(identifier->location.data());
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

std::unique_ptr<raw::Identifier> Parser::ParseIdentifier() {
    auto identifier = ConsumeToken(Token::Kind::Identifier);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Identifier>(identifier.location());
}

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseCompoundIdentifier() {
    std::vector<std::unique_ptr<raw::Identifier>> components;

    components.emplace_back(ParseIdentifier());
    if (!Ok())
        return Fail();

    auto parse_component = [&components, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::Dot:
            ConsumeToken(Token::Kind::Dot);
            if (Ok())
                components.emplace_back(ParseIdentifier());
            return More;
        }
    };

    while (parse_component() == More) {
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::CompoundIdentifier>(std::move(components));
}

std::unique_ptr<raw::StringLiteral> Parser::ParseStringLiteral() {
    auto string_literal = ConsumeToken(Token::Kind::StringLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::StringLiteral>(string_literal.location());
}

std::unique_ptr<raw::NumericLiteral> Parser::ParseNumericLiteral() {
    auto numeric_literal = ConsumeToken(Token::Kind::NumericLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::NumericLiteral>(numeric_literal.location());
}

std::unique_ptr<raw::TrueLiteral> Parser::ParseTrueLiteral() {
    ConsumeToken(Token::Kind::True);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::TrueLiteral>();
}

std::unique_ptr<raw::FalseLiteral> Parser::ParseFalseLiteral() {
    ConsumeToken(Token::Kind::False);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::FalseLiteral>();
}

std::unique_ptr<raw::Literal> Parser::ParseLiteral() {
    switch (Peek()) {
    case Token::Kind::StringLiteral:
        return ParseStringLiteral();

    case Token::Kind::NumericLiteral:
        return ParseNumericLiteral();

    case Token::Kind::True:
        return ParseTrueLiteral();

    case Token::Kind::False:
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
    if (MaybeConsumeToken(Token::Kind::Equal)) {
        value = ParseStringLiteral();
        if (!Ok())
            return Fail();
    }
    return std::make_unique<raw::Attribute>(std::move(name), std::move(value));
}

std::unique_ptr<raw::AttributeList> Parser::ParseAttributeList() {
    ConsumeToken(Token::Kind::LeftSquare);
    if (!Ok())
        return Fail();
    std::vector<std::unique_ptr<raw::Attribute>> attribute_list;
    for (;;) {
        attribute_list.emplace_back(ParseAttribute());
        if (!Ok())
            return Fail();
        if (!MaybeConsumeToken(Token::Kind::Comma))
            break;
    }
    ConsumeToken(Token::Kind::RightSquare);
    if (!Ok())
        return Fail();
    return std::make_unique<raw::AttributeList>(std::move(attribute_list));
}

std::unique_ptr<raw::AttributeList> Parser::MaybeParseAttributeList() {
    if (Peek() == Token::Kind::LeftSquare)
        return ParseAttributeList();
    return nullptr;
}

std::unique_ptr<raw::Constant> Parser::ParseConstant() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
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
    ConsumeToken(Token::Kind::Using);
    if (!Ok())
        return Fail();
    auto using_path = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Identifier> maybe_alias;
    if (MaybeConsumeToken(Token::Kind::As)) {
        if (!Ok())
            return Fail();
        maybe_alias = ParseIdentifier();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::Using>(std::move(using_path), std::move(maybe_alias));
}

std::unique_ptr<raw::ArrayType> Parser::ParseArrayType() {
    ConsumeToken(Token::Kind::Array);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftAngle);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightAngle);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Colon);
    if (!Ok())
        return Fail();
    auto element_count = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ArrayType>(std::move(element_type), std::move(element_count));
}

std::unique_ptr<raw::VectorType> Parser::ParseVectorType() {
    ConsumeToken(Token::Kind::Vector);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftAngle);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightAngle);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::VectorType>(std::move(element_type),
                                             std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::StringType> Parser::ParseStringType() {
    ConsumeToken(Token::Kind::String);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::StringType>(std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::HandleType> Parser::ParseHandleType() {
    ConsumeToken(Token::Kind::Handle);
    if (!Ok())
        return Fail();

    auto subtype = types::HandleSubtype::kHandle;
    if (MaybeConsumeToken(Token::Kind::LeftAngle)) {
        if (!Ok())
            return Fail();
        auto identifier = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!LookupHandleSubtype(identifier.get(), &subtype))
            return Fail();
        ConsumeToken(Token::Kind::RightAngle);
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::HandleType>(subtype, nullability);
}

std::unique_ptr<raw::PrimitiveType> Parser::ParsePrimitiveType() {
    types::PrimitiveSubtype subtype;

    switch (Peek()) {
    case Token::Kind::Bool:
        subtype = types::PrimitiveSubtype::kBool;
        break;
    case Token::Kind::Status:
        subtype = types::PrimitiveSubtype::kStatus;
        break;
    case Token::Kind::Int8:
        subtype = types::PrimitiveSubtype::kInt8;
        break;
    case Token::Kind::Int16:
        subtype = types::PrimitiveSubtype::kInt16;
        break;
    case Token::Kind::Int32:
        subtype = types::PrimitiveSubtype::kInt32;
        break;
    case Token::Kind::Int64:
        subtype = types::PrimitiveSubtype::kInt64;
        break;
    case Token::Kind::Uint8:
        subtype = types::PrimitiveSubtype::kUint8;
        break;
    case Token::Kind::Uint16:
        subtype = types::PrimitiveSubtype::kUint16;
        break;
    case Token::Kind::Uint32:
        subtype = types::PrimitiveSubtype::kUint32;
        break;
    case Token::Kind::Uint64:
        subtype = types::PrimitiveSubtype::kUint64;
        break;
    case Token::Kind::Float32:
        subtype = types::PrimitiveSubtype::kFloat32;
        break;
    case Token::Kind::Float64:
        subtype = types::PrimitiveSubtype::kFloat64;
        break;
    default:
        return Fail();
    }

    ConsumeToken(Peek());
    if (!Ok())
        return Fail();
    return std::make_unique<raw::PrimitiveType>(subtype);
}

std::unique_ptr<raw::RequestHandleType> Parser::ParseRequestHandleType() {
    ConsumeToken(Token::Kind::Request);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftAngle);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightAngle);
    if (!Ok())
        return Fail();

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::RequestHandleType>(std::move(identifier), nullability);
}

std::unique_ptr<raw::Type> Parser::ParseType() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        auto nullability = types::Nullability::kNonnullable;
        if (MaybeConsumeToken(Token::Kind::Question)) {
            if (!Ok())
                return Fail();
            nullability = types::Nullability::kNullable;
        }
        return std::make_unique<raw::IdentifierType>(std::move(identifier), nullability);
    }

    case Token::Kind::Array: {
        auto type = ParseArrayType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::Vector: {
        auto type = ParseVectorType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::String: {
        auto type = ParseStringType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::Handle: {
        auto type = ParseHandleType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::Request: {
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
    ConsumeToken(Token::Kind::Const);
    if (!Ok())
        return Fail();
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Equal);
    if (!Ok())
        return Fail();
    auto constant = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ConstDeclaration>(std::move(attributes), std::move(type),
                                                   std::move(identifier), std::move(constant));
}

std::unique_ptr<raw::EnumMember> Parser::ParseEnumMember() {
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(Token::Kind::Equal);
    if (!Ok())
        return Fail();

    auto member_value = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::EnumMember>(std::move(identifier), std::move(member_value));
}

std::unique_ptr<raw::EnumDeclaration>
Parser::ParseEnumDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::EnumMember>> members;

    ConsumeToken(Token::Kind::Enum);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<raw::PrimitiveType> subtype;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        subtype = ParsePrimitiveType();
        if (!Ok())
            return Fail();
    }
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseEnumMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail();

    return std::make_unique<raw::EnumDeclaration>(std::move(attributes), std::move(identifier),
                                                  std::move(subtype), std::move(members));
}

std::unique_ptr<raw::Parameter> Parser::ParseParameter() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Parameter>(std::move(type), std::move(identifier));
}

std::unique_ptr<raw::ParameterList> Parser::ParseParameterList() {
    std::vector<std::unique_ptr<raw::Parameter>> parameter_list;

    switch (Peek()) {
    default:
        break;

    TOKEN_TYPE_CASES:
        parameter_list.emplace_back(ParseParameter());
        if (!Ok())
            return Fail();
        while (Peek() == Token::Kind::Comma) {
            ConsumeToken(Token::Kind::Comma);
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

    return std::make_unique<raw::ParameterList>(std::move(parameter_list));
}

std::unique_ptr<raw::InterfaceMethod> Parser::ParseInterfaceMethod() {
    auto ordinal = ParseNumericLiteral();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Colon);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Identifier> method_name;
    std::unique_ptr<raw::ParameterList> maybe_request;
    std::unique_ptr<raw::ParameterList> maybe_response;

    auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
        ConsumeToken(Token::Kind::LeftParen);
        if (!Ok())
            return false;
        *params_out = ParseParameterList();
        if (!Ok())
            return false;
        ConsumeToken(Token::Kind::RightParen);
        if (!Ok())
            return false;
        return true;
    };

    if (MaybeConsumeToken(Token::Kind::Arrow)) {
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

        if (MaybeConsumeToken(Token::Kind::Arrow)) {
            if (!Ok())
                return Fail();
            if (!parse_params(&maybe_response))
                return Fail();
        }
    }

    assert(method_name);
    assert(maybe_request || maybe_response);

    return std::make_unique<raw::InterfaceMethod>(std::move(ordinal), std::move(method_name),
                                                  std::move(maybe_request),
                                                  std::move(maybe_response));
}

std::unique_ptr<raw::InterfaceDeclaration>
Parser::ParseInterfaceDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<raw::InterfaceMethod>> methods;

    ConsumeToken(Token::Kind::Interface);
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    if (MaybeConsumeToken(Token::Kind::Colon)) {
        for (;;) {
            superinterfaces.emplace_back(ParseCompoundIdentifier());
            if (!Ok())
                return Fail();
            if (!MaybeConsumeToken(Token::Kind::Comma))
                break;
        }
    }

    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&methods, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        case Token::Kind::NumericLiteral:
            methods.emplace_back(ParseInterfaceMethod());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<raw::InterfaceDeclaration>(std::move(attributes), std::move(identifier),
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
    if (MaybeConsumeToken(Token::Kind::Equal)) {
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::StructMember>(std::move(type), std::move(identifier),
                                               std::move(maybe_default_value));
}

std::unique_ptr<raw::StructDeclaration>
Parser::ParseStructDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::StructMember>> members;

    ConsumeToken(Token::Kind::Struct);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseStructMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail();

    return std::make_unique<raw::StructDeclaration>(std::move(attributes), std::move(identifier),
                                                    std::move(members));
}

std::unique_ptr<raw::UnionMember> Parser::ParseUnionMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::UnionMember>(std::move(type), std::move(identifier));
}

std::unique_ptr<raw::UnionDeclaration>
Parser::ParseUnionDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::UnionMember>> members;

    ConsumeToken(Token::Kind::Union);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseUnionMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        Fail();

    return std::make_unique<raw::UnionDeclaration>(std::move(attributes), std::move(identifier),
                                                   std::move(members));
}

std::unique_ptr<raw::File> Parser::ParseFile() {
    std::vector<std::unique_ptr<raw::Using>> using_list;
    std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<raw::EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<raw::InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<raw::UnionDeclaration>> union_declaration_list;

    ConsumeToken(Token::Kind::Library);
    if (!Ok())
        return Fail();
    auto library_name = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    auto parse_using = [&using_list, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::Using:
            using_list.emplace_back(ParseUsing());
            return More;
        }
    };

    while (parse_using() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::Semicolon);
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

        case Token::Kind::Const:
            const_declaration_list.emplace_back(ParseConstDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::Enum:
            enum_declaration_list.emplace_back(ParseEnumDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::Interface:
            interface_declaration_list.emplace_back(
                ParseInterfaceDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::Struct:
            struct_declaration_list.emplace_back(ParseStructDeclaration(std::move(attributes)));
            return More;

        case Token::Kind::Union:
            union_declaration_list.emplace_back(ParseUnionDeclaration(std::move(attributes)));
            return More;
        }
    };

    while (parse_declaration() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }

    ConsumeToken(Token::Kind::EndOfFile);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::File>(
        std::move(library_name), std::move(using_list), std::move(const_declaration_list),
        std::move(enum_declaration_list), std::move(interface_declaration_list),
        std::move(struct_declaration_list), std::move(union_declaration_list));
}

} // namespace fidl
