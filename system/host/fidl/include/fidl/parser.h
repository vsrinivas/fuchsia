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

    // Each AST node stores the beginning of the code it is associated with, the
    // end of that code, and the end of the previous AST node (so that it can
    // track comments and whitespace between them).
    // This method allows us to track the last SourceLocation that might be
    // interesting to the AST node we're constructing.
    Token LexAndSetPrevious(bool is_discarded, Token& token) {
        backup_end_ = latest_discarded_end_;
        if (is_discarded) {
            if (!latest_discarded_end_.valid()) {
                latest_discarded_end_ = token.previous_end();
            }
        } else {
            if (latest_discarded_end_.valid()) {
                token.set_previous_end(latest_discarded_end_);
            }
            latest_discarded_end_ = SourceLocation();
        }
        return Lex();
    }

    // This consumes a token.  If it is not read on return, is_discarded should
    // be true.  That allows the parser to track its source location, in case it
    // should become interesting to the AST.
    Token ConsumeToken(Token::Kind kind, bool is_discarded = false) {
        if (Peek() != kind)
            Fail();
        auto token = last_token_;
        last_token_ = LexAndSetPrevious(is_discarded, token);
        previous_token_ = token;
        return token;
    }

    bool MaybeConsumeToken(Token::Kind kind) {
        if (Peek() == kind) {
            previous_token_ = last_token_;
            last_token_ = LexAndSetPrevious(true, last_token_);
            return true;
        } else {
            return false;
        }
    }

    // Helper function that figures the earliest token associated with something
    // that can have an attribute list: is it the attribute list, or is it the
    // declaration (struct, enum, etc...)?
    Token ConsumeTokenReturnEarliest(Token::Kind kind,
                                     std::unique_ptr<raw::AttributeList> const& attributes) {
        if (attributes != nullptr && attributes->attribute_list.size() != 0) {
            ConsumeToken(kind, true);
            return attributes->start_;
        }
        return ConsumeToken(kind);
    }

    bool LookupHandleSubtype(const raw::Identifier* identifier, types::HandleSubtype* subtype_out);

    // If the last token seemed to be discarded, but turned out to be important
    // (e.g., a ')' at the end of a parameter list marking the end),
    // retroactively mark it useful again.
    Token MarkLastUseful() {
        if (backup_end_.valid()) {
            previous_token_.set_previous_end(backup_end_);
        }
        latest_discarded_end_ = SourceLocation();
        return previous_token_;
    }

    decltype(nullptr) Fail();

    std::unique_ptr<raw::Identifier> ParseIdentifier(bool is_discarded = false);
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

    // A little explanation of the next three fields.  Each AST node has a
    // pointer to the end of the last non-whitespace, non-comment SourceLocation
    // prior to the beginning of that node.  As the parser walks through tokens,
    // it has to keep track of everything that might be the last non-whitespace,
    // non-comment source location.  That's the latest_discarded_end_ field.
    SourceLocation latest_discarded_end_;

    // As the parser walks the tokens, it discards many of them.  However, it
    // can later realize that the last one it discarded contained the last
    // non-whitespace, non-comment source location. backup_end_ and
    // previous_token_ are used to save and restore that location.
    SourceLocation backup_end_;
    Token previous_token_;

    Token last_token_;
    bool ok_ = true;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_PARSER_H_
