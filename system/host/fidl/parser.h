// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "ast.h"
#include "lexer.h"

namespace fidl {

class Parser {
public:
    explicit Parser(Lexer* lexer)
        : lexer_(lexer) {
        last_token_ = Lex();
    }

    std::unique_ptr<File> Parse() {
        return ParseFile();
    }

    bool Ok() const {
        return ok_;
    }

private:
    Token Lex() {
        return lexer_->LexNoComments();
    }

    Token::Kind Peek() {
        return last_token_.kind();
    }

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

    decltype(nullptr) Fail() {
        ok_ = false;
        return nullptr;
    }

    std::unique_ptr<Identifier> ParseIdentifier();
    std::unique_ptr<CompoundIdentifier> ParseCompoundIdentifier();

    std::unique_ptr<StringLiteral> ParseStringLiteral();
    std::unique_ptr<NumericLiteral> ParseNumericLiteral();
    std::unique_ptr<TrueLiteral> ParseTrueLiteral();
    std::unique_ptr<FalseLiteral> ParseFalseLiteral();
    std::unique_ptr<DefaultLiteral> ParseDefaultLiteral();
    std::unique_ptr<Literal> ParseLiteral();

    std::unique_ptr<Constant> ParseConstant();

    std::unique_ptr<Using> ParseUsing();

    std::unique_ptr<ArrayType> ParseArrayType();
    std::unique_ptr<VectorType> ParseVectorType();
    std::unique_ptr<StringType> ParseStringType();
    std::unique_ptr<HandleType> ParseHandleType();
    std::unique_ptr<PrimitiveType> ParsePrimitiveType();
    std::unique_ptr<RequestType> ParseRequestType();
    std::unique_ptr<Type> ParseType();

    std::unique_ptr<ConstDeclaration> ParseConstDeclaration();

    std::unique_ptr<EnumMember> ParseEnumMember();
    std::unique_ptr<EnumDeclaration> ParseEnumDeclaration();

    std::unique_ptr<Parameter> ParseParameter();
    std::unique_ptr<ParameterList> ParseParameterList();
    std::unique_ptr<InterfaceMemberMethod> ParseInterfaceMemberMethod();
    std::unique_ptr<InterfaceDeclaration> ParseInterfaceDeclaration();

    std::unique_ptr<StructMember> ParseStructMember();
    std::unique_ptr<StructDeclaration> ParseStructDeclaration();

    std::unique_ptr<UnionMember> ParseUnionMember();
    std::unique_ptr<UnionDeclaration> ParseUnionDeclaration();

    std::unique_ptr<File> ParseFile();

    Lexer* lexer_;
    Token last_token_;
    bool ok_ = true;
};

} // namespace fidl
