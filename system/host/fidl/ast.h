// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "token.h"

namespace fidl {

struct Identifier {
    Identifier(Token identifier)
        : identifier(identifier) {}

    Token identifier;
};

struct CompoundIdentifier {
    CompoundIdentifier(std::vector<std::unique_ptr<Identifier>> components)
        : components(std::move(components)) {}

    std::vector<std::unique_ptr<Identifier>> components;
};

struct Literal {
};

struct StringLiteral : public Literal {
    StringLiteral(Token literal)
        : literal(literal) {}

    Token literal;
};

struct NumericLiteral : public Literal {
    NumericLiteral(Token literal)
        : literal(literal) {}

    Token literal;
};

struct TrueLiteral : public Literal {
};

struct FalseLiteral : public Literal {
};

struct DefaultLiteral : public Literal {
};

struct Type {
};

struct HandleType : public Type {
    HandleType(std::unique_ptr<Identifier> maybe_subtype)
        : maybe_subtype(std::move(maybe_subtype)) {}

    std::unique_ptr<Identifier> maybe_subtype;
};

struct RequestType : public Type {
    RequestType(std::unique_ptr<CompoundIdentifier> subtype)
        : subtype(std::move(subtype)) {}

    std::unique_ptr<CompoundIdentifier> subtype;
};

struct IdentifierType : public Type {
    IdentifierType(std::unique_ptr<CompoundIdentifier> identifier)
        : identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;
};

struct PrimitiveType : public Type {
    enum struct TypeKind {
        String,
        Bool,
        Int8,
        Int16,
        Int32,
        Int64,
        Uint8,
        Uint16,
        Uint32,
        Uint64,
        Float32,
        Float64,
    };

    PrimitiveType(TypeKind type_kind)
        : type_kind(type_kind) {}

    TypeKind type_kind;
};

struct Constant {
};

struct IdentifierConstant : Constant {
    IdentifierConstant(std::unique_ptr<CompoundIdentifier> identifier)
        : identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;
};

struct LiteralConstant : Constant {
    LiteralConstant(std::unique_ptr<Literal> literal)
        : literal(std::move(literal)) {}

    std::unique_ptr<Literal> literal;
};

struct ModuleName {
    ModuleName(std::unique_ptr<CompoundIdentifier> identifier)
        : identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;
};

struct Using {
    Using(std::unique_ptr<StringLiteral> import_path)
        : import_path(std::move(import_path)) {}

    std::unique_ptr<StringLiteral> import_path;
};

struct UsingList {
    UsingList(std::vector<std::unique_ptr<Using>> import_list)
        : import_list(std::move(import_list)) {}

    std::vector<std::unique_ptr<Using>> import_list;
};

struct Declaration {
};

struct ConstDeclaration : public Declaration {
    ConstDeclaration(std::unique_ptr<Type> type,
                     std::unique_ptr<Identifier> identifier,
                     std::unique_ptr<Constant> constant)
        : type(std::move(type)),
          identifier(std::move(identifier)),
          constant(std::move(constant)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> constant;
};

struct EnumMemberValue {
};

struct EnumMemberValueIdentifier : public EnumMemberValue {
    EnumMemberValueIdentifier(std::unique_ptr<CompoundIdentifier> identifier)
        : identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;
};

struct EnumMemberValueNumeric : public EnumMemberValue {
    EnumMemberValueNumeric(std::unique_ptr<NumericLiteral> literal)
        : literal(std::move(literal)) {}

    std::unique_ptr<NumericLiteral> literal;
};

struct EnumMember {
    EnumMember(std::unique_ptr<Identifier> identifier,
               std::unique_ptr<EnumMemberValue> maybe_value)
        : identifier(std::move(identifier)),
          maybe_value(std::move(maybe_value)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<EnumMemberValue> maybe_value;
};

struct EnumBody {
    EnumBody(std::vector<std::unique_ptr<EnumMember>> fields)
        : fields(std::move(fields)) {}

    std::vector<std::unique_ptr<EnumMember>> fields;
};

struct EnumDeclaration : public Declaration {
    EnumDeclaration(std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<PrimitiveType> maybe_subtype,
                    std::unique_ptr<EnumBody> body)
        : identifier(std::move(identifier)),
          maybe_subtype(std::move(maybe_subtype)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<PrimitiveType> maybe_subtype;
    std::unique_ptr<EnumBody> body;
};

struct InterfaceMember {
};

struct InterfaceMemberConst : public InterfaceMember {
    InterfaceMemberConst(std::unique_ptr<ConstDeclaration> const_declaration)
        : const_declaration(std::move(const_declaration)) {}

    std::unique_ptr<ConstDeclaration> const_declaration;
};

struct InterfaceMemberEnum : public InterfaceMember {
    InterfaceMemberEnum(std::unique_ptr<EnumDeclaration> enum_declaration)
        : enum_declaration(std::move(enum_declaration)) {}

    std::unique_ptr<EnumDeclaration> enum_declaration;
};

struct Parameter {
    Parameter(std::unique_ptr<Type> type,
              std::unique_ptr<Identifier> identifier)
        : type(std::move(type)),
          identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

struct ParameterList {
    ParameterList(std::vector<std::unique_ptr<Parameter>> parameter_list)
        : parameter_list(std::move(parameter_list)) {}

    std::vector<std::unique_ptr<Parameter>> parameter_list;
};

struct Response {
    Response(std::unique_ptr<ParameterList> parameter_list)
        : parameter_list(std::move(parameter_list)) {}

    std::unique_ptr<ParameterList> parameter_list;
};

struct InterfaceMemberMethod : public InterfaceMember {
    InterfaceMemberMethod(std::unique_ptr<NumericLiteral> ordinal,
                          std::unique_ptr<Identifier> identifier,
                          std::unique_ptr<ParameterList> parameter_list,
                          std::unique_ptr<Response> maybe_response)
        : ordinal(std::move(ordinal)),
          identifier(std::move(identifier)),
          parameter_list(std::move(parameter_list)),
          maybe_response(std::move(maybe_response)) {}

    std::unique_ptr<NumericLiteral> ordinal;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<ParameterList> parameter_list;
    std::unique_ptr<Response> maybe_response;
};

struct InterfaceBody {
    InterfaceBody(std::vector<std::unique_ptr<InterfaceMember>> fields)
        : fields(std::move(fields)) {}

    std::vector<std::unique_ptr<InterfaceMember>> fields;
};

struct InterfaceDeclaration : public Declaration {
    InterfaceDeclaration(std::unique_ptr<Identifier> identifier,
                         std::unique_ptr<InterfaceBody> body)
        : identifier(std::move(identifier)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<InterfaceBody> body;
};

struct StructMember {
};

struct StructMemberConst : public StructMember {
    StructMemberConst(std::unique_ptr<ConstDeclaration> const_declaration)
        : const_declaration(std::move(const_declaration)) {}

    std::unique_ptr<ConstDeclaration> const_declaration;
};

struct StructMemberEnum : public StructMember {
    StructMemberEnum(std::unique_ptr<EnumDeclaration> enum_declaration)
        : enum_declaration(std::move(enum_declaration)) {}

    std::unique_ptr<EnumDeclaration> enum_declaration;
};

struct StructDefaultValue {
    StructDefaultValue(std::unique_ptr<Constant> const_declaration)
        : const_declaration(std::move(const_declaration)) {}

    std::unique_ptr<Constant> const_declaration;
};

struct StructMemberField : public StructMember {
    StructMemberField(std::unique_ptr<Type> type,
                      std::unique_ptr<Identifier> identifier,
                      std::unique_ptr<StructDefaultValue> maybe_default_value)
        : type(std::move(type)),
          identifier(std::move(identifier)),
          maybe_default_value(std::move(maybe_default_value)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<StructDefaultValue> maybe_default_value;
};

struct StructBody {
    StructBody(std::vector<std::unique_ptr<StructMember>> fields)
        : fields(std::move(fields)) {}

    std::vector<std::unique_ptr<StructMember>> fields;
};

struct StructDeclaration : public Declaration {
    StructDeclaration(std::unique_ptr<Identifier> identifier,
                      std::unique_ptr<StructBody> body)
        : identifier(std::move(identifier)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<StructBody> body;
};

struct UnionMember {
    UnionMember(std::unique_ptr<Type> type,
                std::unique_ptr<Identifier> identifier)
        : type(std::move(type)),
          identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

struct UnionBody {
    UnionBody(std::vector<std::unique_ptr<UnionMember>> fields)
        : fields(std::move(fields)) {}

    std::vector<std::unique_ptr<UnionMember>> fields;
};

struct UnionDeclaration : public Declaration {
    UnionDeclaration(std::unique_ptr<Identifier> identifier,
                     std::unique_ptr<UnionBody> body)
        : identifier(std::move(identifier)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<UnionBody> body;
};

struct DeclarationList {
    DeclarationList(std::vector<std::unique_ptr<Declaration>> declaration_list)
        : declaration_list(std::move(declaration_list)) {}

    std::vector<std::unique_ptr<Declaration>> declaration_list;
};

struct File {
    File(std::unique_ptr<ModuleName> module,
         std::unique_ptr<UsingList> import_list,
         std::unique_ptr<DeclarationList> declaration_list)
        : module(std::move(module)),
          import_list(std::move(import_list)),
          declaration_list(std::move(declaration_list)) {}

    std::unique_ptr<ModuleName> module;
    std::unique_ptr<UsingList> import_list;
    std::unique_ptr<DeclarationList> declaration_list;
};

} // namespace fidl
