// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

namespace fidl {

#define TOKEN_PRIMITIVE_TYPE_CASES \
    case Token::Kind::String:      \
    case Token::Kind::Bool:        \
    case Token::Kind::Int8:        \
    case Token::Kind::Int16:       \
    case Token::Kind::Int32:       \
    case Token::Kind::Int64:       \
    case Token::Kind::Uint8:       \
    case Token::Kind::Uint16:      \
    case Token::Kind::Uint32:      \
    case Token::Kind::Uint64:      \
    case Token::Kind::Float32:     \
    case Token::Kind::Float64

#define TOKEN_TYPE_CASES          \
    TOKEN_PRIMITIVE_TYPE_CASES:   \
    case Token::Kind::Identifier: \
    case Token::Kind::Handle:     \
    case Token::Kind::Request

#define TOKEN_LITERAL_CASES           \
    case Token::Kind::Default:        \
    case Token::Kind::True:           \
    case Token::Kind::False:          \
    case Token::Kind::NumericLiteral: \
    case Token::Kind::StringLiteral

std::unique_ptr<Identifier> Parser::ParseIdentifier() {
    auto identifier = ConsumeToken(Token::Kind::Identifier);
    if (!Ok())
        return Fail();

    return std::make_unique<Identifier>(identifier);
}

std::unique_ptr<CompoundIdentifier> Parser::ParseCompoundIdentifier() {
    std::vector<std::unique_ptr<Identifier>> components;

    components.emplace_back(ParseIdentifier());
    if (!Ok())
        return Fail();

    for (;;) {
        switch (Peek()) {
        default:
            return std::make_unique<CompoundIdentifier>(std::move(components));

        case Token::Kind::Dot:
            ConsumeToken(Token::Kind::Dot);
            if (!Ok())
                return Fail();
            components.emplace_back(ParseIdentifier());
            if (!Ok())
                return Fail();
            break;
        }
    }
}

std::unique_ptr<StringLiteral> Parser::ParseStringLiteral() {
    auto string_literal = ConsumeToken(Token::Kind::StringLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<StringLiteral>(string_literal);
}

std::unique_ptr<NumericLiteral> Parser::ParseNumericLiteral() {
    auto numeric_literal = ConsumeToken(Token::Kind::NumericLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<NumericLiteral>(numeric_literal);
}

std::unique_ptr<TrueLiteral> Parser::ParseTrueLiteral() {
    ConsumeToken(Token::Kind::True);
    if (!Ok())
        return Fail();

    return std::make_unique<TrueLiteral>();
}

std::unique_ptr<FalseLiteral> Parser::ParseFalseLiteral() {
    ConsumeToken(Token::Kind::False);
    if (!Ok())
        return Fail();

    return std::make_unique<FalseLiteral>();
}

std::unique_ptr<DefaultLiteral> Parser::ParseDefaultLiteral() {
    ConsumeToken(Token::Kind::Default);
    if (!Ok())
        return Fail();

    return std::make_unique<DefaultLiteral>();
}

std::unique_ptr<Literal> Parser::ParseLiteral() {
    switch (Peek()) {
    case Token::Kind::StringLiteral:
        return ParseStringLiteral();

    case Token::Kind::NumericLiteral:
        return ParseNumericLiteral();

    case Token::Kind::True:
        return ParseTrueLiteral();

    case Token::Kind::False:
        return ParseFalseLiteral();

    case Token::Kind::Default:
        return ParseDefaultLiteral();

    default:
        return Fail();
    }
}

std::unique_ptr<Constant> Parser::ParseConstant() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        return std::make_unique<IdentifierConstant>(std::move(identifier));
    }

    TOKEN_LITERAL_CASES : {
        auto literal = ParseLiteral();
        if (!Ok())
            return Fail();
        return std::make_unique<LiteralConstant>(std::move(literal));
    }

    default:
        return Fail();
    }
}

std::unique_ptr<ModuleName> Parser::ParseModuleName() {
    if (PeekFor(Token::Kind::Module)) {
        ConsumeToken(Token::Kind::Module);
        if (!Ok())
            return Fail();
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
        return std::make_unique<ModuleName>(std::move(identifier));
    }

    return nullptr;
}

std::unique_ptr<Using> Parser::ParseUsing() {
    ConsumeToken(Token::Kind::Using);
    if (!Ok())
        return Fail();
    auto literal = ParseStringLiteral();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<Using>(std::move(literal));
}

std::unique_ptr<UsingList> Parser::ParseUsingList() {
    std::vector<std::unique_ptr<Using>> import_list;

    for (;;) {
        switch (Peek()) {
        default:
            return std::make_unique<UsingList>(std::move(import_list));

        case Token::Kind::Using:
            import_list.emplace_back(ParseUsing());
            if (!Ok())
                return Fail();
            break;
        }
    }
}

std::unique_ptr<HandleType> Parser::ParseHandleType() {
    ConsumeToken(Token::Kind::Handle);
    if (!Ok())
        return Fail();

    std::unique_ptr<Identifier> identifier;

    if (PeekFor(Token::Kind::LeftAngle)) {
        ConsumeToken(Token::Kind::LeftAngle);
        if (!Ok())
            return Fail();
        identifier = ParseIdentifier();
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::RightAngle);
        if (!Ok())
            return Fail();
    }

    return std::make_unique<HandleType>(std::move(identifier));
}

std::unique_ptr<PrimitiveType> Parser::ParsePrimitiveType() {
    PrimitiveType::TypeKind type_kind;

    switch (Peek()) {
    case Token::Kind::String:
        type_kind = PrimitiveType::TypeKind::String;
        break;
    case Token::Kind::Bool:
        type_kind = PrimitiveType::TypeKind::Bool;
        break;
    case Token::Kind::Int8:
        type_kind = PrimitiveType::TypeKind::Int8;
        break;
    case Token::Kind::Int16:
        type_kind = PrimitiveType::TypeKind::Int16;
        break;
    case Token::Kind::Int32:
        type_kind = PrimitiveType::TypeKind::Int32;
        break;
    case Token::Kind::Int64:
        type_kind = PrimitiveType::TypeKind::Int64;
        break;
    case Token::Kind::Uint8:
        type_kind = PrimitiveType::TypeKind::Uint8;
        break;
    case Token::Kind::Uint16:
        type_kind = PrimitiveType::TypeKind::Uint16;
        break;
    case Token::Kind::Uint32:
        type_kind = PrimitiveType::TypeKind::Uint32;
        break;
    case Token::Kind::Uint64:
        type_kind = PrimitiveType::TypeKind::Uint64;
        break;
    case Token::Kind::Float32:
        type_kind = PrimitiveType::TypeKind::Float32;
        break;
    case Token::Kind::Float64:
        type_kind = PrimitiveType::TypeKind::Float64;
        break;
    default:
        return Fail();
    }

    ConsumeToken(Peek());
    if (!Ok())
        return Fail();
    return std::make_unique<PrimitiveType>(type_kind);
}

std::unique_ptr<RequestType> Parser::ParseRequestType() {
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

    return std::make_unique<RequestType>(std::move(identifier));
}

std::unique_ptr<Type> Parser::ParseType() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        return std::make_unique<IdentifierType>(std::move(identifier));
    }

    case Token::Kind::Handle: {
        auto type = ParseHandleType();
        if (!Ok())
            return Fail();
        return type;
    }

    case Token::Kind::Request: {
        auto type = ParseRequestType();
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

std::unique_ptr<ConstDeclaration> Parser::ParseConstDeclaration() {
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
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<ConstDeclaration>(std::move(type),
                                              std::move(identifier),
                                              std::move(constant));
}

std::unique_ptr<EnumMember> Parser::ParseEnumMember() {
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<EnumMemberValue> field_value;

    if (PeekFor(Token::Kind::Equal)) {
        ConsumeToken(Token::Kind::Equal);
        if (!Ok())
            return Fail();

        switch (Peek()) {
        case Token::Kind::Identifier: {
            auto compound_identifier = ParseCompoundIdentifier();
            if (!Ok())
                return Fail();
            field_value = std::make_unique<EnumMemberValueIdentifier>(std::move(compound_identifier));
            break;
        }

        case Token::Kind::NumericLiteral: {
            auto literal = ParseNumericLiteral();
            if (!Ok())
                return Fail();
            field_value = std::make_unique<EnumMemberValueNumeric>(std::move(literal));
            break;
        }

        default:
            return Fail();
        }
    }

    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<EnumMember>(std::move(identifier),
                                        std::move(field_value));
}

std::unique_ptr<EnumBody> Parser::ParseEnumBody() {
    std::vector<std::unique_ptr<EnumMember>> fields;

    for (;;) {
        switch (Peek()) {
        default:
            return std::make_unique<EnumBody>(std::move(fields));

        TOKEN_TYPE_CASES:
            fields.emplace_back(ParseEnumMember());
            if (!Ok())
                return Fail();
            break;
        }
    }
}

std::unique_ptr<EnumDeclaration> Parser::ParseEnumDeclaration() {
    ConsumeToken(Token::Kind::Enum);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<PrimitiveType> subtype;
    if (PeekFor(Token::Kind::Colon)) {
        ConsumeToken(Token::Kind::Colon);
        if (!Ok())
            return Fail();
        subtype = ParsePrimitiveType();
        if (!Ok())
            return Fail();
    }
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();
    auto body = ParseEnumBody();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightCurly);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<EnumDeclaration>(std::move(identifier),
                                             std::move(subtype),
                                             std::move(body));
}

std::unique_ptr<InterfaceMemberConst> Parser::ParseInterfaceMemberConst() {
    auto const_decl = ParseConstDeclaration();
    if (!Ok())
        return Fail();

    return std::make_unique<InterfaceMemberConst>(std::move(const_decl));
}

std::unique_ptr<InterfaceMemberEnum> Parser::ParseInterfaceMemberEnum() {
    auto enum_decl = ParseEnumDeclaration();
    if (!Ok())
        return Fail();

    return std::make_unique<InterfaceMemberEnum>(std::move(enum_decl));
}

std::unique_ptr<Parameter> Parser::ParseParameter() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<Parameter>(std::move(type),
                                       std::move(identifier));
}

std::unique_ptr<ParameterList> Parser::ParseParameterList() {
    std::vector<std::unique_ptr<Parameter>> parameter_list;

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

    return std::make_unique<ParameterList>(std::move(parameter_list));
}

std::unique_ptr<Response> Parser::ParseResponse() {
    if (PeekFor(Token::Kind::Arrow)) {
        ConsumeToken(Token::Kind::Arrow);
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::LeftParen);
        if (!Ok())
            return Fail();
        auto parameter_list = ParseParameterList();
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::RightParen);
        if (!Ok())
            return Fail();

        return std::make_unique<Response>(std::move(parameter_list));
    }

    return nullptr;
}

std::unique_ptr<InterfaceMemberMethod> Parser::ParseInterfaceMemberMethod() {
    auto ordinal = ParseNumericLiteral();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Colon);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftParen);
    if (!Ok())
        return Fail();
    auto parameter_list = ParseParameterList();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightParen);
    if (!Ok())
        return Fail();
    auto response = ParseResponse();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<InterfaceMemberMethod>(std::move(ordinal),
                                                   std::move(identifier),
                                                   std::move(parameter_list),
                                                   std::move(response));
}

std::unique_ptr<InterfaceBody> Parser::ParseInterfaceBody() {
    std::vector<std::unique_ptr<InterfaceMember>> fields;

    for (;;) {
        switch (Peek()) {
        default:
            return std::make_unique<InterfaceBody>(std::move(fields));

        case Token::Kind::Const:
            fields.emplace_back(ParseInterfaceMemberConst());
            if (!Ok())
                return Fail();
            break;

        case Token::Kind::Enum:
            fields.emplace_back(ParseInterfaceMemberEnum());
            if (!Ok())
                return Fail();
            break;

        case Token::Kind::NumericLiteral:
            fields.emplace_back(ParseInterfaceMemberMethod());
            if (!Ok())
                return Fail();
            break;
        }
    }
}

std::unique_ptr<InterfaceDeclaration> Parser::ParseInterfaceDeclaration() {
    ConsumeToken(Token::Kind::Interface);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();
    auto body = ParseInterfaceBody();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightCurly);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<InterfaceDeclaration>(std::move(identifier),
                                                  std::move(body));
}

std::unique_ptr<StructMemberConst> Parser::ParseStructMemberConst() {
    auto const_decl = ParseConstDeclaration();
    if (!Ok())
        return Fail();

    return std::make_unique<StructMemberConst>(std::move(const_decl));
}

std::unique_ptr<StructMemberEnum> Parser::ParseStructMemberEnum() {
    auto enum_decl = ParseEnumDeclaration();
    if (!Ok())
        return Fail();

    return std::make_unique<StructMemberEnum>(std::move(enum_decl));
}

std::unique_ptr<StructDefaultValue> Parser::ParseStructDefaultValue() {
    if (PeekFor(Token::Kind::Equal)) {
        ConsumeToken(Token::Kind::Equal);
        if (!Ok())
            return Fail();
        auto constant = ParseConstant();
        if (!Ok())
            return Fail();

        return std::make_unique<StructDefaultValue>(std::move(constant));
    }

    return nullptr;
}

std::unique_ptr<StructMemberField> Parser::ParseStructMemberField() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    auto default_value = ParseStructDefaultValue();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<StructMemberField>(std::move(type),
                                               std::move(identifier),
                                               std::move(default_value));
}

std::unique_ptr<StructBody> Parser::ParseStructBody() {
    std::vector<std::unique_ptr<StructMember>> fields;

    for (;;) {
        switch (Peek()) {
        default:
            return std::make_unique<StructBody>(std::move(fields));

        case Token::Kind::Const:
            fields.emplace_back(ParseStructMemberConst());
            if (!Ok())
                return Fail();
            break;

        case Token::Kind::Enum:
            fields.emplace_back(ParseStructMemberEnum());
            if (!Ok())
                return Fail();
            break;

        TOKEN_TYPE_CASES:
            fields.emplace_back(ParseStructMemberField());
            if (!Ok())
                return Fail();
            break;
        }
    }
}

std::unique_ptr<StructDeclaration> Parser::ParseStructDeclaration() {
    ConsumeToken(Token::Kind::Struct);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();
    auto body = ParseStructBody();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightCurly);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<StructDeclaration>(std::move(identifier),
                                               std::move(body));
}

std::unique_ptr<UnionMember> Parser::ParseUnionMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<UnionMember>(std::move(type),
                                         std::move(identifier));
}

std::unique_ptr<UnionBody> Parser::ParseUnionBody() {
    std::vector<std::unique_ptr<UnionMember>> fields;

    for (;;) {
        switch (Peek()) {
        default:
            return std::make_unique<UnionBody>(std::move(fields));

        TOKEN_TYPE_CASES:
            fields.emplace_back(ParseUnionMember());
            if (!Ok())
                return Fail();
            break;
        }
    }
}

std::unique_ptr<UnionDeclaration> Parser::ParseUnionDeclaration() {
    ConsumeToken(Token::Kind::Union);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();
    auto body = ParseUnionBody();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::RightCurly);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Semicolon);
    if (!Ok())
        return Fail();

    return std::make_unique<UnionDeclaration>(std::move(identifier),
                                              std::move(body));
}

std::unique_ptr<DeclarationList> Parser::ParseDeclarationList() {
    std::vector<std::unique_ptr<Declaration>> declaration_list;

    for (;;) {
        switch (Peek()) {
        default:
            return std::make_unique<DeclarationList>(std::move(declaration_list));

        case Token::Kind::Const:
            declaration_list.emplace_back(ParseConstDeclaration());
            if (!Ok())
                return Fail();
            break;

        case Token::Kind::Enum:
            declaration_list.emplace_back(ParseEnumDeclaration());
            if (!Ok())
                return Fail();
            break;

        case Token::Kind::Interface:
            declaration_list.emplace_back(ParseInterfaceDeclaration());
            if (!Ok())
                return Fail();
            break;

        case Token::Kind::Struct:
            declaration_list.emplace_back(ParseStructDeclaration());
            if (!Ok())
                return Fail();
            break;

        case Token::Kind::Union:
            declaration_list.emplace_back(ParseUnionDeclaration());
            if (!Ok())
                return Fail();
            break;
        }
    }
}

std::unique_ptr<File> Parser::ParseFile() {
    auto module_name = ParseModuleName();
    if (!Ok())
        return Fail();
    auto import_list = ParseUsingList();
    if (!Ok())
        return Fail();
    auto declaration_list = ParseDeclarationList();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::EndOfFile);
    if (!Ok())
        return Fail();

    return std::make_unique<File>(std::move(module_name),
                                  std::move(import_list),
                                  std::move(declaration_list));
}

} // namespace fidl
