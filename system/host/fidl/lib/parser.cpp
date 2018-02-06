// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/parser.h"

namespace fidl {

#define TOKEN_PRIMITIVE_TYPE_CASES \
    case Token::Kind::Bool:        \
    case Token::Kind::Status:      \
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
    case Token::Kind::Array:      \
    case Token::Kind::Vector:     \
    case Token::Kind::String:     \
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

decltype(nullptr) Parser::Fail() {
    if (ok_) {
        int line_number;
        auto surrounding_line = last_token_.location().SourceLine(&line_number);

        std::string error = "found unexpected token: ";
        error += last_token_.data();
        error += "\n";
        error += "on line #" + std::to_string(line_number) + ":\n\n";
        error += surrounding_line;
        error += "\n";

        error_reporter_->ReportError(error);
        ok_ = false;
    }
    return nullptr;
}

std::unique_ptr<ast::Identifier> Parser::ParseIdentifier() {
    auto identifier = ConsumeToken(Token::Kind::Identifier);
    if (!Ok())
        return Fail();

    return std::make_unique<ast::Identifier>(identifier.location());
}

std::unique_ptr<ast::CompoundIdentifier> Parser::ParseCompoundIdentifier() {
    std::vector<std::unique_ptr<ast::Identifier>> components;

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

    return std::make_unique<ast::CompoundIdentifier>(std::move(components));
}

std::unique_ptr<ast::StringLiteral> Parser::ParseStringLiteral() {
    auto string_literal = ConsumeToken(Token::Kind::StringLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<ast::StringLiteral>(string_literal.location());
}

std::unique_ptr<ast::NumericLiteral> Parser::ParseNumericLiteral() {
    auto numeric_literal = ConsumeToken(Token::Kind::NumericLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<ast::NumericLiteral>(numeric_literal.location());
}

std::unique_ptr<ast::TrueLiteral> Parser::ParseTrueLiteral() {
    ConsumeToken(Token::Kind::True);
    if (!Ok())
        return Fail();

    return std::make_unique<ast::TrueLiteral>();
}

std::unique_ptr<ast::FalseLiteral> Parser::ParseFalseLiteral() {
    ConsumeToken(Token::Kind::False);
    if (!Ok())
        return Fail();

    return std::make_unique<ast::FalseLiteral>();
}

std::unique_ptr<ast::DefaultLiteral> Parser::ParseDefaultLiteral() {
    ConsumeToken(Token::Kind::Default);
    if (!Ok())
        return Fail();

    return std::make_unique<ast::DefaultLiteral>();
}

std::unique_ptr<ast::Literal> Parser::ParseLiteral() {
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

std::unique_ptr<ast::Constant> Parser::ParseConstant() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        return std::make_unique<ast::IdentifierConstant>(std::move(identifier));
    }

    TOKEN_LITERAL_CASES : {
        auto literal = ParseLiteral();
        if (!Ok())
            return Fail();
        return std::make_unique<ast::LiteralConstant>(std::move(literal));
    }

    default:
        return Fail();
    }
}

std::unique_ptr<ast::Using> Parser::ParseUsing() {
    ConsumeToken(Token::Kind::Using);
    if (!Ok())
        return Fail();
    auto using_path = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<ast::Identifier> maybe_alias;
    if (MaybeConsumeToken(Token::Kind::As)) {
        if (!Ok())
            return Fail();
        maybe_alias = ParseIdentifier();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<ast::Using>(std::move(using_path), std::move(maybe_alias));
}

std::unique_ptr<ast::ArrayType> Parser::ParseArrayType() {
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

    return std::make_unique<ast::ArrayType>(std::move(element_type), std::move(element_count));
}

std::unique_ptr<ast::VectorType> Parser::ParseVectorType() {
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

    std::unique_ptr<ast::Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = ast::Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = ast::Nullability::Nullable;
    }

    return std::make_unique<ast::VectorType>(std::move(element_type),
                                             std::move(maybe_element_count), nullability);
}

std::unique_ptr<ast::StringType> Parser::ParseStringType() {
    ConsumeToken(Token::Kind::String);
    if (!Ok())
        return Fail();

    std::unique_ptr<ast::Constant> maybe_element_count;
    if (MaybeConsumeToken(Token::Kind::Colon)) {
        if (!Ok())
            return Fail();
        maybe_element_count = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = ast::Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = ast::Nullability::Nullable;
    }

    return std::make_unique<ast::StringType>(std::move(maybe_element_count), nullability);
}

std::unique_ptr<ast::HandleType> Parser::ParseHandleType() {
    ConsumeToken(Token::Kind::Handle);
    if (!Ok())
        return Fail();

    auto subtype = types::HandleSubtype::Handle;

    if (MaybeConsumeToken(Token::Kind::LeftAngle)) {
        if (!Ok())
            return Fail();
        switch (Peek()) {
        case Token::Kind::Process:
            subtype = types::HandleSubtype::Process;
            break;
        case Token::Kind::Thread:
            subtype = types::HandleSubtype::Thread;
            break;
        case Token::Kind::Vmo:
            subtype = types::HandleSubtype::Vmo;
            break;
        case Token::Kind::Channel:
            subtype = types::HandleSubtype::Channel;
            break;
        case Token::Kind::Event:
            subtype = types::HandleSubtype::Event;
            break;
        case Token::Kind::Port:
            subtype = types::HandleSubtype::Port;
            break;
        case Token::Kind::Interrupt:
            subtype = types::HandleSubtype::Interrupt;
            break;
        case Token::Kind::Iomap:
            subtype = types::HandleSubtype::Iomap;
            break;
        case Token::Kind::Pci:
            subtype = types::HandleSubtype::Pci;
            break;
        case Token::Kind::Log:
            subtype = types::HandleSubtype::Log;
            break;
        case Token::Kind::Socket:
            subtype = types::HandleSubtype::Socket;
            break;
        case Token::Kind::Resource:
            subtype = types::HandleSubtype::Resource;
            break;
        case Token::Kind::Eventpair:
            subtype = types::HandleSubtype::Eventpair;
            break;
        case Token::Kind::Job:
            subtype = types::HandleSubtype::Job;
            break;
        case Token::Kind::Vmar:
            subtype = types::HandleSubtype::Vmar;
            break;
        case Token::Kind::Fifo:
            subtype = types::HandleSubtype::Fifo;
            break;
        case Token::Kind::Hypervisor:
            subtype = types::HandleSubtype::Hypervisor;
            break;
        case Token::Kind::Guest:
            subtype = types::HandleSubtype::Guest;
            break;
        case Token::Kind::Timer:
            subtype = types::HandleSubtype::Timer;
            break;
        default:
            return Fail();
        }
        Consume();
        if (!Ok())
            return Fail();

        ConsumeToken(Token::Kind::RightAngle);
        if (!Ok())
            return Fail();
    }

    auto nullability = ast::Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = ast::Nullability::Nullable;
    }

    return std::make_unique<ast::HandleType>(subtype, nullability);
}

std::unique_ptr<ast::PrimitiveType> Parser::ParsePrimitiveType() {
    ast::PrimitiveType::Subtype subtype;

    switch (Peek()) {
    case Token::Kind::Bool:
        subtype = ast::PrimitiveType::Subtype::Bool;
        break;
    case Token::Kind::Status:
        subtype = ast::PrimitiveType::Subtype::Status;
        break;
    case Token::Kind::Int8:
        subtype = ast::PrimitiveType::Subtype::Int8;
        break;
    case Token::Kind::Int16:
        subtype = ast::PrimitiveType::Subtype::Int16;
        break;
    case Token::Kind::Int32:
        subtype = ast::PrimitiveType::Subtype::Int32;
        break;
    case Token::Kind::Int64:
        subtype = ast::PrimitiveType::Subtype::Int64;
        break;
    case Token::Kind::Uint8:
        subtype = ast::PrimitiveType::Subtype::Uint8;
        break;
    case Token::Kind::Uint16:
        subtype = ast::PrimitiveType::Subtype::Uint16;
        break;
    case Token::Kind::Uint32:
        subtype = ast::PrimitiveType::Subtype::Uint32;
        break;
    case Token::Kind::Uint64:
        subtype = ast::PrimitiveType::Subtype::Uint64;
        break;
    case Token::Kind::Float32:
        subtype = ast::PrimitiveType::Subtype::Float32;
        break;
    case Token::Kind::Float64:
        subtype = ast::PrimitiveType::Subtype::Float64;
        break;
    default:
        return Fail();
    }

    ConsumeToken(Peek());
    if (!Ok())
        return Fail();
    return std::make_unique<ast::PrimitiveType>(subtype);
}

std::unique_ptr<ast::RequestType> Parser::ParseRequestType() {
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

    auto nullability = ast::Nullability::Nonnullable;
    if (MaybeConsumeToken(Token::Kind::Question)) {
        nullability = ast::Nullability::Nullable;
    }

    return std::make_unique<ast::RequestType>(std::move(identifier), nullability);
}

std::unique_ptr<ast::Type> Parser::ParseType() {
    switch (Peek()) {
    case Token::Kind::Identifier: {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        auto nullability = ast::Nullability::Nonnullable;
        if (MaybeConsumeToken(Token::Kind::Question)) {
            if (!Ok())
                return Fail();
            nullability = ast::Nullability::Nullable;
        }
        return std::make_unique<ast::IdentifierType>(std::move(identifier), nullability);
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

std::unique_ptr<ast::ConstDeclaration> Parser::ParseConstDeclaration() {
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

    return std::make_unique<ast::ConstDeclaration>(std::move(type), std::move(identifier),
                                                   std::move(constant));
}

std::unique_ptr<ast::EnumMember> Parser::ParseEnumMember() {
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(Token::Kind::Equal);
    if (!Ok())
        return Fail();

    auto member_value = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<ast::EnumMember>(std::move(identifier), std::move(member_value));
}

std::unique_ptr<ast::EnumDeclaration> Parser::ParseEnumDeclaration() {
    std::vector<std::unique_ptr<ast::EnumMember>> members;

    ConsumeToken(Token::Kind::Enum);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<ast::PrimitiveType> subtype;
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

    return std::make_unique<ast::EnumDeclaration>(std::move(identifier), std::move(subtype),
                                                  std::move(members));
}

std::unique_ptr<ast::Parameter> Parser::ParseParameter() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<ast::Parameter>(std::move(type), std::move(identifier));
}

std::unique_ptr<ast::ParameterList> Parser::ParseParameterList() {
    std::vector<std::unique_ptr<ast::Parameter>> parameter_list;

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

    return std::make_unique<ast::ParameterList>(std::move(parameter_list));
}

std::unique_ptr<ast::InterfaceMemberMethod> Parser::ParseInterfaceMemberMethod() {
    auto ordinal = ParseNumericLiteral();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::Colon);
    if (!Ok())
        return Fail();

    std::unique_ptr<ast::Identifier> method_name;
    std::unique_ptr<ast::ParameterList> maybe_request;
    std::unique_ptr<ast::ParameterList> maybe_response;

    auto parse_params = [this](std::unique_ptr<ast::ParameterList>* params_out) {
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

    if (MaybeConsumeToken(Token::Kind::Event)) {
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

    return std::make_unique<ast::InterfaceMemberMethod>(std::move(ordinal),
                                                        std::move(method_name),
                                                        std::move(maybe_request),
                                                        std::move(maybe_response));
}

std::unique_ptr<ast::InterfaceDeclaration> Parser::ParseInterfaceDeclaration() {
    std::vector<std::unique_ptr<ast::CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<ast::ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<ast::EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<ast::InterfaceMemberMethod>> method_members;

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
    if (!Ok())
        Fail();

    return std::make_unique<ast::InterfaceDeclaration>(std::move(identifier), std::move(superinterfaces),
                                                       std::move(const_members), std::move(enum_members),
                                                       std::move(method_members));
}

std::unique_ptr<ast::StructMember> Parser::ParseStructMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<ast::Constant> maybe_default_value;
    if (MaybeConsumeToken(Token::Kind::Equal)) {
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<ast::StructMember>(std::move(type), std::move(identifier),
                                               std::move(maybe_default_value));
}

std::unique_ptr<ast::StructDeclaration> Parser::ParseStructDeclaration() {
    std::vector<std::unique_ptr<ast::ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<ast::EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<ast::StructMember>> members;

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
    if (!Ok())
        Fail();

    return std::make_unique<ast::StructDeclaration>(std::move(identifier), std::move(const_members),
                                                    std::move(enum_members), std::move(members));
}

std::unique_ptr<ast::UnionMember> Parser::ParseUnionMember() {
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<ast::UnionMember>(std::move(type), std::move(identifier));
}

std::unique_ptr<ast::UnionDeclaration> Parser::ParseUnionDeclaration() {
    std::vector<std::unique_ptr<ast::ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<ast::EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<ast::UnionMember>> members;

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
    if (!Ok())
        Fail();

    return std::make_unique<ast::UnionDeclaration>(std::move(identifier), std::move(const_members),
                                                   std::move(enum_members), std::move(members));
}

std::unique_ptr<ast::File> Parser::ParseFile() {
    std::vector<std::unique_ptr<ast::Using>> using_list;
    std::vector<std::unique_ptr<ast::ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<ast::EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<ast::InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<ast::StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<ast::UnionDeclaration>> union_declaration_list;

    ConsumeToken(Token::Kind::Library);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
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
        ConsumeToken(Token::Kind::Semicolon);
        if (!Ok())
            return Fail();
    }

    ConsumeToken(Token::Kind::EndOfFile);
    if (!Ok())
        return Fail();

    return std::make_unique<ast::File>(
        std::move(identifier), std::move(using_list), std::move(const_declaration_list),
        std::move(enum_declaration_list), std::move(interface_declaration_list),
        std::move(struct_declaration_list), std::move(union_declaration_list));
}

} // namespace fidl
