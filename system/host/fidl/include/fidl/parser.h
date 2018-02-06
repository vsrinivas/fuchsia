// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_PARSER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_PARSER_H_

#include <memory>

#include "ast.h"
#include "error_reporter.h"
#include "lexer.h"

namespace fidl {

class Parser {
public:
    Parser(Lexer* lexer, ErrorReporter* error_reporter)
        : lexer_(lexer)
        , error_reporter_(error_reporter) {
        last_token_ = Lex();
    }

    std::unique_ptr<ast::File> Parse() { return ParseFile(); }

    bool Ok() const { return ok_; }

private:
    Token Lex() { return lexer_->LexNoComments(); }

    Token::Kind Peek() { return last_token_.kind(); }

    Token Consume() {
        auto token = last_token_;
        last_token_ = Lex();
        return token;
    }

    Token ConsumeToken(Token::Kind kind) {
        auto token = Consume();
        if (token.kind() != kind)
            Fail();
        return token;
    }

    bool MaybeConsumeToken(Token::Kind kind) {
        if (Peek() == kind) {
            auto token = Consume();
            static_cast<void>(token);
            assert(token.kind() == kind);
            return true;
        } else {
            return false;
        }
    }

    decltype(nullptr) Fail();

    std::unique_ptr<ast::Identifier> ParseIdentifier();
    std::unique_ptr<ast::CompoundIdentifier> ParseCompoundIdentifier();

    std::unique_ptr<ast::StringLiteral> ParseStringLiteral();
    std::unique_ptr<ast::NumericLiteral> ParseNumericLiteral();
    std::unique_ptr<ast::TrueLiteral> ParseTrueLiteral();
    std::unique_ptr<ast::FalseLiteral> ParseFalseLiteral();
    std::unique_ptr<ast::DefaultLiteral> ParseDefaultLiteral();
    std::unique_ptr<ast::Literal> ParseLiteral();

    std::unique_ptr<ast::Constant> ParseConstant();

    std::unique_ptr<ast::Using> ParseUsing();

    std::unique_ptr<ast::ArrayType> ParseArrayType();
    std::unique_ptr<ast::VectorType> ParseVectorType();
    std::unique_ptr<ast::StringType> ParseStringType();
    std::unique_ptr<ast::HandleType> ParseHandleType();
    std::unique_ptr<ast::PrimitiveType> ParsePrimitiveType();
    std::unique_ptr<ast::RequestType> ParseRequestType();
    std::unique_ptr<ast::Type> ParseType();

    std::unique_ptr<ast::ConstDeclaration> ParseConstDeclaration();

    std::unique_ptr<ast::EnumMember> ParseEnumMember();
    std::unique_ptr<ast::EnumDeclaration> ParseEnumDeclaration();

    std::unique_ptr<ast::Parameter> ParseParameter();
    std::unique_ptr<ast::ParameterList> ParseParameterList();
    std::unique_ptr<ast::InterfaceMemberMethod> ParseInterfaceMemberMethod();
    std::unique_ptr<ast::InterfaceDeclaration> ParseInterfaceDeclaration();

    std::unique_ptr<ast::StructMember> ParseStructMember();
    std::unique_ptr<ast::StructDeclaration> ParseStructDeclaration();

    std::unique_ptr<ast::UnionMember> ParseUnionMember();
    std::unique_ptr<ast::UnionDeclaration> ParseUnionDeclaration();

    std::unique_ptr<ast::File> ParseFile();

    Lexer* lexer_;
    ErrorReporter* error_reporter_;

    Token last_token_;
    bool ok_ = true;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_PARSER_H_
