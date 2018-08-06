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

std::unique_ptr<raw::Identifier> Parser::ParseIdentifier() {
    auto identifier = ConsumeToken(Token::Kind::kIdentifier);
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

        case Token::Kind::kDot:
            ConsumeToken(Token::Kind::kDot);
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
    auto string_literal = ConsumeToken(Token::Kind::kStringLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::StringLiteral>(string_literal.location());
}

std::unique_ptr<raw::NumericLiteral> Parser::ParseNumericLiteral() {
    auto numeric_literal = ConsumeToken(Token::Kind::kNumericLiteral);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::NumericLiteral>(numeric_literal.location());
}

std::unique_ptr<raw::TrueLiteral> Parser::ParseTrueLiteral() {
    ConsumeToken(Token::Kind::kTrue);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::TrueLiteral>();
}

std::unique_ptr<raw::FalseLiteral> Parser::ParseFalseLiteral() {
    ConsumeToken(Token::Kind::kFalse);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::FalseLiteral>();
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
    return std::make_unique<raw::Attribute>(std::move(name), std::move(value));
}

std::unique_ptr<raw::AttributeList> Parser::ParseAttributeList() {
    ConsumeToken(Token::Kind::kLeftSquare);
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
    ConsumeToken(Token::Kind::kRightSquare);
    if (!Ok())
        return Fail();
    return std::make_unique<raw::AttributeList>(std::move(attribute_list));
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
    ConsumeToken(Token::Kind::kUsing);
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

    return std::make_unique<raw::Using>(std::move(using_path), std::move(maybe_alias), std::move(maybe_primitive));
}

std::unique_ptr<raw::ArrayType> Parser::ParseArrayType() {
    ConsumeToken(Token::Kind::kArray);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftAngle);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kRightAngle);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kColon);
    if (!Ok())
        return Fail();
    auto element_count = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ArrayType>(std::move(element_type), std::move(element_count));
}

std::unique_ptr<raw::VectorType> Parser::ParseVectorType() {
    ConsumeToken(Token::Kind::kVector);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftAngle);
    if (!Ok())
        return Fail();
    auto element_type = ParseType();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kRightAngle);
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

    return std::make_unique<raw::VectorType>(std::move(element_type),
                                             std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::StringType> Parser::ParseStringType() {
    ConsumeToken(Token::Kind::kString);
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

    return std::make_unique<raw::StringType>(std::move(maybe_element_count), nullability);
}

std::unique_ptr<raw::HandleType> Parser::ParseHandleType() {
    ConsumeToken(Token::Kind::kHandle);
    if (!Ok())
        return Fail();

    auto subtype = types::HandleSubtype::kHandle;
    if (MaybeConsumeToken(Token::Kind::kLeftAngle)) {
        if (!Ok())
            return Fail();
        auto identifier = ParseIdentifier();
        if (!Ok())
            return Fail();
        if (!LookupHandleSubtype(identifier.get(), &subtype))
            return Fail();
        ConsumeToken(Token::Kind::kRightAngle);
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::kQuestion)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::HandleType>(subtype, nullability);
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

    ConsumeToken(Peek());
    if (!Ok())
        return Fail();
    return std::make_unique<raw::PrimitiveType>(subtype);
}

std::unique_ptr<raw::RequestHandleType> Parser::ParseRequestHandleType() {
    ConsumeToken(Token::Kind::kRequest);
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftAngle);
    if (!Ok())
        return Fail();
    auto identifier = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kRightAngle);
    if (!Ok())
        return Fail();

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(Token::Kind::kQuestion)) {
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::RequestHandleType>(std::move(identifier), nullability);
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
        return std::make_unique<raw::IdentifierType>(std::move(identifier), nullability);
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
    ConsumeToken(Token::Kind::kConst);
    if (!Ok())
        return Fail();
    auto type = ParseType();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kEqual);
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

    ConsumeToken(Token::Kind::kEqual);
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

    ConsumeToken(Token::Kind::kEnum);
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
    ConsumeToken(Token::Kind::kLeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseEnumMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon);
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
        while (Peek() == Token::Kind::kComma) {
            ConsumeToken(Token::Kind::kComma);
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

std::unique_ptr<raw::InterfaceMethod> Parser::ParseInterfaceMethod(std::unique_ptr<raw::AttributeList> attributes) {
    auto ordinal = ParseNumericLiteral();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kColon);
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Identifier> method_name;
    std::unique_ptr<raw::ParameterList> maybe_request;
    std::unique_ptr<raw::ParameterList> maybe_response;

    auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
        ConsumeToken(Token::Kind::kLeftParen);
        if (!Ok())
            return false;
        *params_out = ParseParameterList();
        if (!Ok())
            return false;
        ConsumeToken(Token::Kind::kRightParen);
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

    return std::make_unique<raw::InterfaceMethod>(std::move(attributes),
                                                  std::move(ordinal),
                                                  std::move(method_name),
                                                  std::move(maybe_request),
                                                  std::move(maybe_response));
}

std::unique_ptr<raw::InterfaceDeclaration>
Parser::ParseInterfaceDeclaration(std::unique_ptr<raw::AttributeList> attributes) {
    std::vector<std::unique_ptr<raw::CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<raw::InterfaceMethod>> methods;

    ConsumeToken(Token::Kind::kInterface);
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

    ConsumeToken(Token::Kind::kLeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&methods, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly);
            return Done;

        case Token::Kind::kNumericLiteral:
            methods.emplace_back(ParseInterfaceMethod(std::move(attributes)));
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon);
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
    if (MaybeConsumeToken(Token::Kind::kEqual)) {
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

    ConsumeToken(Token::Kind::kStruct);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseStructMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon);
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

    ConsumeToken(Token::Kind::kUnion);
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLeftCurly);
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek()) {
        default:
            ConsumeToken(Token::Kind::kRightCurly);
            return Done;

        TOKEN_TYPE_CASES:
            members.emplace_back(ParseUnionMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(Token::Kind::kSemicolon);
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

    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kLibrary);
    if (!Ok())
        return Fail();
    auto library_name = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();
    ConsumeToken(Token::Kind::kSemicolon);
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
        ConsumeToken(Token::Kind::kSemicolon);
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
        ConsumeToken(Token::Kind::kSemicolon);
        if (!Ok())
            return Fail();
    }

    ConsumeToken(Token::Kind::kEndOfFile);
    if (!Ok())
        return Fail();

    return std::make_unique<raw::File>(
        std::move(attributes), std::move(library_name), std::move(using_list),
        std::move(const_declaration_list), std::move(enum_declaration_list),
        std::move(interface_declaration_list), std::move(struct_declaration_list),
        std::move(union_declaration_list));
}

} // namespace fidl
