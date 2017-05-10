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

struct Using {
    Using(std::unique_ptr<CompoundIdentifier> using_path,
          std::unique_ptr<Identifier> maybe_alias)
        : using_path(std::move(using_path)),
          maybe_alias(std::move(maybe_alias)) {}

    std::unique_ptr<CompoundIdentifier> using_path;
    std::unique_ptr<Identifier> maybe_alias;
};

struct ConstDeclaration {
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

struct EnumDeclaration {
    EnumDeclaration(std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<PrimitiveType> maybe_subtype,
                    std::vector<std::unique_ptr<EnumMember>> members)
        : identifier(std::move(identifier)),
          maybe_subtype(std::move(maybe_subtype)),
          members(std::move(members)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<PrimitiveType> maybe_subtype;
    std::vector<std::unique_ptr<EnumMember>> members;
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

struct InterfaceMemberMethod {
    InterfaceMemberMethod(std::unique_ptr<NumericLiteral> ordinal,
                          std::unique_ptr<Identifier> identifier,
                          std::unique_ptr<ParameterList> parameter_list,
                          std::unique_ptr<ParameterList> maybe_response)
        : ordinal(std::move(ordinal)),
          identifier(std::move(identifier)),
          parameter_list(std::move(parameter_list)),
          maybe_response(std::move(maybe_response)) {}

    std::unique_ptr<NumericLiteral> ordinal;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<ParameterList> parameter_list;
    std::unique_ptr<ParameterList> maybe_response;
};

struct InterfaceDeclaration {
    InterfaceDeclaration(std::unique_ptr<Identifier> identifier,
                         std::vector<std::unique_ptr<ConstDeclaration>> const_members,
                         std::vector<std::unique_ptr<EnumDeclaration>> enum_members,
                         std::vector<std::unique_ptr<InterfaceMemberMethod>> method_members)
        : identifier(std::move(identifier)),
          const_members(std::move(const_members)),
          enum_members(std::move(enum_members)),
          method_members(std::move(method_members)) {}

    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<InterfaceMemberMethod>> method_members;
};

struct StructMember {
    StructMember(std::unique_ptr<Type> type,
                      std::unique_ptr<Identifier> identifier,
                      std::unique_ptr<Constant> maybe_default_value)
        : type(std::move(type)),
          identifier(std::move(identifier)),
          maybe_default_value(std::move(maybe_default_value)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> maybe_default_value;
};

struct StructDeclaration {
    StructDeclaration(std::unique_ptr<Identifier> identifier,
                      std::vector<std::unique_ptr<ConstDeclaration>> const_members,
                      std::vector<std::unique_ptr<EnumDeclaration>> enum_members,
                      std::vector<std::unique_ptr<StructMember>> members)
        : identifier(std::move(identifier)),
          const_members(std::move(const_members)),
          enum_members(std::move(enum_members)),
          members(std::move(members)) {}

    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<StructMember>> members;
};

struct UnionMember {
    UnionMember(std::unique_ptr<Type> type,
                std::unique_ptr<Identifier> identifier)
        : type(std::move(type)),
          identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

struct UnionDeclaration {
    UnionDeclaration(std::unique_ptr<Identifier> identifier,
                     std::vector<std::unique_ptr<ConstDeclaration>> const_members,
                     std::vector<std::unique_ptr<EnumDeclaration>> enum_members,
                     std::vector<std::unique_ptr<UnionMember>> members)
        : identifier(std::move(identifier)),
          const_members(std::move(const_members)),
          enum_members(std::move(enum_members)),
          members(std::move(members)) {}

    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<ConstDeclaration>> const_members;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_members;
    std::vector<std::unique_ptr<UnionMember>> members;
};

struct File {
    File(std::unique_ptr<CompoundIdentifier> identifier,
         std::vector<std::unique_ptr<Using>> using_list,
         std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list,
         std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list,
         std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list,
         std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list,
         std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list)
        : identifier(std::move(identifier)),
          using_list(std::move(using_list)),
          const_declaration_list(std::move(const_declaration_list)),
          enum_declaration_list(std::move(enum_declaration_list)),
          interface_declaration_list(std::move(interface_declaration_list)),
          struct_declaration_list(std::move(struct_declaration_list)),
          union_declaration_list(std::move(union_declaration_list)) {}

    std::unique_ptr<CompoundIdentifier> identifier;
    std::vector<std::unique_ptr<Using>> using_list;
    std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list;
};

} // namespace fidl
