// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_PARSER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_PARSER_H_

#include <memory>

#include "error_reporter.h"
#include "lexer.h"
#include "raw_ast.h"

namespace fidl {

class Parser {
public:
    Parser(Lexer* lexer, ErrorReporter* error_reporter);

    std::unique_ptr<raw::File> Parse() { return ParseFile(); }

    bool Ok() const { return ok_; }

private:
    Token Lex() { return lexer_->LexNoComments(); }

    Token::Kind Peek() { return last_token_.kind(); }

    Token ConsumeToken(Token::Kind kind) {
        if (Peek() != kind)
            Fail();
        auto token = last_token_;
        last_token_ = Lex();
        return token;
    }

    bool MaybeConsumeToken(Token::Kind kind) {
        if (Peek() == kind) {
            last_token_ = Lex();
            return true;
        } else {
            return false;
        }
    }

    bool LookupHandleSubtype(const raw::Identifier* identifier, types::HandleSubtype* subtype_out);

    decltype(nullptr) Fail();

    std::unique_ptr<raw::Identifier> ParseIdentifier();
    std::unique_ptr<raw::CompoundIdentifier> ParseCompoundIdentifier();

    std::unique_ptr<raw::StringLiteral> ParseStringLiteral();
    std::unique_ptr<raw::NumericLiteral> ParseNumericLiteral();
    std::unique_ptr<raw::TrueLiteral> ParseTrueLiteral();
    std::unique_ptr<raw::FalseLiteral> ParseFalseLiteral();
    std::unique_ptr<raw::Literal> ParseLiteral();

    std::unique_ptr<raw::Constant> ParseConstant();

    std::unique_ptr<raw::Attribute> ParseAttribute();
    std::unique_ptr<raw::AttributeList> ParseAttributeList();
    std::unique_ptr<raw::AttributeList> MaybeParseAttributeList();

    std::unique_ptr<raw::Using> ParseUsing();

    std::unique_ptr<raw::ArrayType> ParseArrayType();
    std::unique_ptr<raw::VectorType> ParseVectorType();
    std::unique_ptr<raw::StringType> ParseStringType();
    std::unique_ptr<raw::HandleType> ParseHandleType();
    std::unique_ptr<raw::PrimitiveType> ParsePrimitiveType();
    std::unique_ptr<raw::RequestHandleType> ParseRequestHandleType();
    std::unique_ptr<raw::Type> ParseType();

    std::unique_ptr<raw::ConstDeclaration>
    ParseConstDeclaration(std::unique_ptr<raw::AttributeList> attributes);

    std::unique_ptr<raw::EnumMember> ParseEnumMember();
    std::unique_ptr<raw::EnumDeclaration>
    ParseEnumDeclaration(std::unique_ptr<raw::AttributeList> attributes);

    std::unique_ptr<raw::Parameter> ParseParameter();
    std::unique_ptr<raw::ParameterList> ParseParameterList();
    std::unique_ptr<raw::InterfaceMethod> ParseInterfaceMethod(
        std::unique_ptr<raw::AttributeList> attributes);
    std::unique_ptr<raw::InterfaceDeclaration>
    ParseInterfaceDeclaration(std::unique_ptr<raw::AttributeList> attributes);

    std::unique_ptr<raw::StructMember> ParseStructMember();
    std::unique_ptr<raw::StructDeclaration>
    ParseStructDeclaration(std::unique_ptr<raw::AttributeList> attributes);

    std::unique_ptr<raw::UnionMember> ParseUnionMember();
    std::unique_ptr<raw::UnionDeclaration>
    ParseUnionDeclaration(std::unique_ptr<raw::AttributeList> attributes);

    std::unique_ptr<raw::File> ParseFile();

    std::map<StringView, types::HandleSubtype> handle_subtype_table_;

    Lexer* lexer_;
    ErrorReporter* error_reporter_;

    Token last_token_;
    bool ok_ = true;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_PARSER_H_
