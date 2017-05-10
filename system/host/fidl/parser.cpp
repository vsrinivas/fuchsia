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

namespace {
enum {
    More,
    Done,
};
} // namespace

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

    return std::make_unique<CompoundIdentifier>(std::move(components));
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

std::unique_ptr<Using> Parser::ParseUsing() {
    ConsumeToken(Token::Kind::Using);
    if (!Ok())
        return Fail();
    auto using_path = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<Identifier> maybe_alias;
    if (PeekFor(Token::Kind::As)) {
        ConsumeToken(Token::Kind::As);
        if (!Ok())
            return Fail();
        maybe_alias = ParseIdentifier();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<Using>(std::move(using_path), std::move(maybe_alias));
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

    return std::make_unique<ConstDeclaration>(std::move(type),
                                              std::move(identifier),
                                              std::move(constant));
}

std::unique_ptr<EnumMember> Parser::ParseEnumMember() {
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<EnumMemberValue> member_value;

    if (PeekFor(Token::Kind::Equal)) {
        ConsumeToken(Token::Kind::Equal);
        if (!Ok())
            return Fail();

        switch (Peek()) {
        case Token::Kind::Identifier: {
            auto compound_identifier = ParseCompoundIdentifier();
            if (!Ok())
                return Fail();
            member_value = std::make_unique<EnumMemberValueIdentifier>(std::move(compound_identifier));
            break;
        }

        case Token::Kind::NumericLiteral: {
            auto literal = ParseNumericLiteral();
            if (!Ok())
                return Fail();
            member_value = std::make_unique<EnumMemberValueNumeric>(std::move(literal));
            break;
        }

        default:
            return Fail();
        }
    }

    return std::make_unique<EnumMember>(std::move(identifier),
                                        std::move(member_value));
}

std::unique_ptr<EnumDeclaration> Parser::ParseEnumDeclaration() {
    std::vector<std::unique_ptr<EnumMember>> members;

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

    return std::make_unique<EnumDeclaration>(std::move(identifier),
                                             std::move(subtype),
                                             std::move(members));
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

    std::unique_ptr<ParameterList> maybe_response;
    if (PeekFor(Token::Kind::Arrow)) {
        ConsumeToken(Token::Kind::Arrow);
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::LeftParen);
        if (!Ok())
            return Fail();
        maybe_response = ParseParameterList();
        if (!Ok())
            return Fail();
        ConsumeToken(Token::Kind::RightParen);
        if (!Ok())
            return Fail();
    }

    return std::make_unique<InterfaceMemberMethod>(std::move(ordinal),
                                                   std::move(identifier),
                                                   std::move(parameter_list),
                                                   std::move(maybe_response));
}

std::unique_ptr<InterfaceDeclaration> Parser::ParseInterfaceDeclaration() {
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<InterfaceMemberMethod>> method_members;

    ConsumeToken(Token::Kind::Interface);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&const_members, &enum_members, &method_members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        case Token::Kind::Const:
            const_members.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_members.emplace_back(ParseEnumDeclaration());
            return More;

        case Token::Kind::NumericLiteral:
            method_members.emplace_back(ParseInterfaceMemberMethod());
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

    return std::make_unique<InterfaceDeclaration>(std::move(identifier),
                                                  std::move(const_members),
                                                  std::move(enum_members),
                                                  std::move(method_members));
}

std::unique_ptr<StructMember> Parser::ParseStructMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<Constant> maybe_default_value;
    if (PeekFor(Token::Kind::Equal)) {
        ConsumeToken(Token::Kind::Equal);
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<StructMember>(std::move(type),
                                          std::move(identifier),
                                          std::move(maybe_default_value));
}

std::unique_ptr<StructDeclaration> Parser::ParseStructDeclaration() {
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<StructMember>> members;

    ConsumeToken(Token::Kind::Struct);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&const_members, &enum_members, &members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        case Token::Kind::Const:
            const_members.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_members.emplace_back(ParseEnumDeclaration());
            return More;

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

    return std::make_unique<StructDeclaration>(std::move(identifier),
                                               std::move(const_members),
                                               std::move(enum_members),
                                               std::move(members));
}

std::unique_ptr<UnionMember> Parser::ParseUnionMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<UnionMember>(std::move(type),
                                         std::move(identifier));
}

std::unique_ptr<UnionDeclaration> Parser::ParseUnionDeclaration() {
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<UnionMember>> members;

    ConsumeToken(Token::Kind::Union);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::LeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&const_members, &enum_members, &members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::RightCurly);
            return Done;

        case Token::Kind::Const:
            const_members.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_members.emplace_back(ParseEnumDeclaration());
            return More;

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

    return std::make_unique<UnionDeclaration>(std::move(identifier),
                                              std::move(const_members),
                                              std::move(enum_members),
                                              std::move(members));
}

std::unique_ptr<File> Parser::ParseFile() {
    std::vector<std::unique_ptr<Using>> using_list;
    std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list;

    ConsumeToken(Token::Kind::Module);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
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
    }

    auto parse_declaration = [&const_declaration_list, &enum_declaration_list,
                              &interface_declaration_list, &struct_declaration_list,
                              &union_declaration_list, this]() {
        switch (Peek()) {
        default:
            return Done;

        case Token::Kind::Const:
            const_declaration_list.emplace_back(ParseConstDeclaration());
            return More;

        case Token::Kind::Enum:
            enum_declaration_list.emplace_back(ParseEnumDeclaration());
            return More;

        case Token::Kind::Interface:
            interface_declaration_list.emplace_back(ParseInterfaceDeclaration());
            return More;

        case Token::Kind::Struct:
            struct_declaration_list.emplace_back(ParseStructDeclaration());
            return More;

        case Token::Kind::Union:
            union_declaration_list.emplace_back(ParseUnionDeclaration());
            return More;
        }
    };

    while (parse_declaration() == More) {
        if (!Ok())
            return Fail();
    }

    ConsumeToken(Token::Kind::EndOfFile);
    if (!Ok())
        return Fail();

    return std::make_unique<File>(std::move(identifier),
                                  std::move(using_list),
                                  std::move(const_declaration_list),
                                  std::move(enum_declaration_list),
                                  std::move(interface_declaration_list),
                                  std::move(struct_declaration_list),
                                  std::move(union_declaration_list));
}

} // namespace fidl
