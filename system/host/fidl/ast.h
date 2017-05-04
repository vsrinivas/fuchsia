// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "token.h"

namespace fidl {

struct Node {
    enum struct Kind : uint8_t {
        Identifier,
        CompoundIdentifier,
        // Literal,
        StringLiteral,
        NumericLiteral,
        TrueLiteral,
        FalseLiteral,
        DefaultLiteral,
        // Type,
        HandleType,
        IdentifierType,
        PrimitiveType,
        RequestType,
        // Constant,
        LiteralConstant,
        IdentifierConstant,
        ModuleName,
        Using,
        UsingList,
        // EnumMemberValue,
        EnumMemberValueIdentifier,
        EnumMemberValueNumeric,
        EnumMember,
        EnumBody,
        Parameter,
        ParameterList,
        Response,
        // InterfaceMember,
        InterfaceMemberConst,
        InterfaceMemberEnum,
        InterfaceMemberMethod,
        InterfaceBody,
        StructDefaultValue,
        // StructMember,
        StructMemberConst,
        StructMemberEnum,
        StructMemberField,
        StructBody,
        UnionMember,
        UnionBody,
        // Declaration,
        ConstDeclaration,
        EnumDeclaration,
        InterfaceDeclaration,
        StructDeclaration,
        UnionDeclaration,
        DeclarationList,
        File,
    };

    Node(Kind kind)
        : kind(kind) {}

    Kind kind;
};

template <typename NodeType>
NodeType* try_cast(Node* node) {
    if (NodeType::isinstance(node))
        return static_cast<NodeType*>(node);
    return nullptr;
}

struct Identifier : public Node {
    Identifier(Token identifier)
        : Node(Kind::Identifier),
          identifier(identifier) {}

    Token identifier;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::Identifier;
    }
};

struct CompoundIdentifier : public Node {
    CompoundIdentifier(std::vector<std::unique_ptr<Identifier>> components)
        : Node(Kind::CompoundIdentifier),
          components(std::move(components)) {}

    std::vector<std::unique_ptr<Identifier>> components;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::CompoundIdentifier;
    }
};

struct Literal : public Node {
    Literal(Kind kind)
        : Node(kind) {}
};

struct StringLiteral : public Literal {
    StringLiteral(Token literal)
        : Literal(Kind::StringLiteral),
          literal(literal) {}

    Token literal;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::StringLiteral;
    }
};

struct NumericLiteral : public Literal {
    NumericLiteral(Token literal)
        : Literal(Kind::NumericLiteral),
          literal(literal) {}

    Token literal;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::NumericLiteral;
    }
};

struct TrueLiteral : public Literal {
    TrueLiteral()
        : Literal(Kind::TrueLiteral) {}

    static bool isinstance(const Node* node) {
        return node->kind == Kind::TrueLiteral;
    }
};

struct FalseLiteral : public Literal {
    FalseLiteral()
        : Literal(Kind::FalseLiteral) {}

    static bool isinstance(const Node* node) {
        return node->kind == Kind::FalseLiteral;
    }
};

struct DefaultLiteral : public Literal {
    DefaultLiteral()
        : Literal(Kind::DefaultLiteral) {}

    static bool isinstance(const Node* node) {
        return node->kind == Kind::DefaultLiteral;
    }
};

struct Type : public Node {
    Type(Kind kind)
        : Node(kind) {}

    static bool isinstance(const Node* node) {
        switch (node->kind) {
        case Kind::HandleType:
        case Kind::IdentifierType:
        case Kind::PrimitiveType:
        case Kind::RequestType:
            return true;

        default:
            return false;
        }
    }
};

struct HandleType : public Type {
    HandleType(std::unique_ptr<Identifier> maybe_subtype)
        : Type(Kind::HandleType),
          maybe_subtype(std::move(maybe_subtype)) {}

    std::unique_ptr<Identifier> maybe_subtype;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::HandleType;
    }
};

struct RequestType : public Type {
    RequestType(std::unique_ptr<CompoundIdentifier> subtype)
        : Type(Kind::RequestType),
          subtype(std::move(subtype)) {}

    std::unique_ptr<CompoundIdentifier> subtype;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::RequestType;
    }
};

struct IdentifierType : public Type {
    IdentifierType(std::unique_ptr<CompoundIdentifier> identifier)
        : Type(Kind::IdentifierType),
          identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::IdentifierType;
    }
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
        : Type(Node::Kind::PrimitiveType),
          type_kind(type_kind) {}

    TypeKind type_kind;

    static bool isinstance(const Node* node) {
        return node->kind == Node::Kind::PrimitiveType;
    }
};

struct Constant : public Node {
    Constant(Kind kind)
        : Node(kind) {}

    static bool isinstance(const Node* node) {
        switch (node->kind) {
        case Kind::LiteralConstant:
        case Kind::IdentifierConstant:
            return true;

        default:
            return false;
        }
    }
};

struct IdentifierConstant : Constant {
    IdentifierConstant(std::unique_ptr<CompoundIdentifier> identifier)
        : Constant(Kind::IdentifierConstant),
          identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::IdentifierConstant;
    }
};

struct LiteralConstant : Constant {
    LiteralConstant(std::unique_ptr<Literal> literal)
        : Constant(Kind::LiteralConstant),
          literal(std::move(literal)) {}

    std::unique_ptr<Literal> literal;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::LiteralConstant;
    }
};

struct ModuleName : public Node {
    ModuleName(std::unique_ptr<CompoundIdentifier> identifier)
        : Node(Kind::ModuleName),
          identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::ModuleName;
    }
};

struct Using : public Node {
    Using(std::unique_ptr<StringLiteral> import_path)
        : Node(Kind::Using),
          import_path(std::move(import_path)) {}

    std::unique_ptr<StringLiteral> import_path;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::Using;
    }
};

struct UsingList : public Node {
    UsingList(std::vector<std::unique_ptr<Using>> import_list)
        : Node(Kind::UsingList),
          import_list(std::move(import_list)) {}

    std::vector<std::unique_ptr<Using>> import_list;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::UsingList;
    }
};

struct Declaration : public Node {
    Declaration(Kind kind)
        : Node(kind) {}

    static bool isinstance(const Node* node) {
        switch (node->kind) {
        case Kind::ConstDeclaration:
        case Kind::EnumDeclaration:
        case Kind::InterfaceDeclaration:
        case Kind::StructDeclaration:
        case Kind::UnionDeclaration:
            return true;

        default:
            return false;
        }
    }
};

struct ConstDeclaration : public Declaration {
    ConstDeclaration(std::unique_ptr<Type> type,
                     std::unique_ptr<Identifier> identifier,
                     std::unique_ptr<Constant> constant)
        : Declaration(Kind::ConstDeclaration),
          type(std::move(type)),
          identifier(std::move(identifier)),
          constant(std::move(constant)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> constant;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::ConstDeclaration;
    }
};

struct EnumMemberValue : public Node {
    EnumMemberValue(Kind kind)
        : Node(kind) {}

    static bool isinstance(const Node* node) {
        switch (node->kind) {
        case Kind::EnumMemberValueIdentifier:
        case Kind::EnumMemberValueNumeric:
            return true;

        default:
            return false;
        }
    }
};

struct EnumMemberValueIdentifier : public EnumMemberValue {
    EnumMemberValueIdentifier(std::unique_ptr<CompoundIdentifier> identifier)
        : EnumMemberValue(Kind::EnumMemberValueIdentifier),
          identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::EnumMemberValueIdentifier;
    }
};

struct EnumMemberValueNumeric : public EnumMemberValue {
    EnumMemberValueNumeric(std::unique_ptr<NumericLiteral> literal)
        : EnumMemberValue(Kind::EnumMemberValueNumeric),
          literal(std::move(literal)) {}

    std::unique_ptr<NumericLiteral> literal;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::EnumMemberValueNumeric;
    }
};

struct EnumMember : public Node {
    EnumMember(std::unique_ptr<Identifier> identifier,
               std::unique_ptr<EnumMemberValue> maybe_value)
        : Node(Kind::EnumMember),
          identifier(std::move(identifier)),
          maybe_value(std::move(maybe_value)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<EnumMemberValue> maybe_value;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::EnumMember;
    }
};

struct EnumBody : public Node {
    EnumBody(std::vector<std::unique_ptr<EnumMember>> fields)
        : Node(Kind::EnumBody),
          fields(std::move(fields)) {}

    std::vector<std::unique_ptr<EnumMember>> fields;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::EnumBody;
    }
};

struct EnumDeclaration : public Declaration {
    EnumDeclaration(std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<PrimitiveType> maybe_subtype,
                    std::unique_ptr<EnumBody> body)
        : Declaration(Kind::EnumDeclaration),
          identifier(std::move(identifier)),
          maybe_subtype(std::move(maybe_subtype)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<PrimitiveType> maybe_subtype;
    std::unique_ptr<EnumBody> body;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::EnumDeclaration;
    }
};

struct InterfaceMember : public Node {
    InterfaceMember(Kind kind)
        : Node(kind) {}

    static bool isinstance(const Node* node) {
        switch (node->kind) {
        case Kind::InterfaceMemberConst:
        case Kind::InterfaceMemberEnum:
        case Kind::InterfaceMemberMethod:
            return true;

        default:
            return false;
        }
    }
};

struct InterfaceMemberConst : public InterfaceMember {
    InterfaceMemberConst(std::unique_ptr<ConstDeclaration> const_declaration)
        : InterfaceMember(Kind::InterfaceMemberConst),
          const_declaration(std::move(const_declaration)) {}

    std::unique_ptr<ConstDeclaration> const_declaration;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::InterfaceMemberConst;
    }
};

struct InterfaceMemberEnum : public InterfaceMember {
    InterfaceMemberEnum(std::unique_ptr<EnumDeclaration> enum_declaration)
        : InterfaceMember(Kind::InterfaceMemberEnum),
          enum_declaration(std::move(enum_declaration)) {}

    std::unique_ptr<EnumDeclaration> enum_declaration;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::InterfaceMemberEnum;
    }
};

struct Parameter : public Node {
    Parameter(std::unique_ptr<Type> type,
              std::unique_ptr<Identifier> identifier)
        : Node(Kind::Parameter),
          type(std::move(type)),
          identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::Parameter;
    }
};

struct ParameterList : public Node {
    ParameterList(std::vector<std::unique_ptr<Parameter>> parameter_list)
        : Node(Kind::ParameterList),
          parameter_list(std::move(parameter_list)) {}

    std::vector<std::unique_ptr<Parameter>> parameter_list;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::ParameterList;
    }
};

struct Response : public Node {
    Response(std::unique_ptr<ParameterList> parameter_list)
        : Node(Kind::Response),
          parameter_list(std::move(parameter_list)) {}

    std::unique_ptr<ParameterList> parameter_list;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::Response;
    }
};

struct InterfaceMemberMethod : public InterfaceMember {
    InterfaceMemberMethod(std::unique_ptr<NumericLiteral> ordinal,
                          std::unique_ptr<Identifier> identifier,
                          std::unique_ptr<ParameterList> parameter_list,
                          std::unique_ptr<Response> maybe_response)
        : InterfaceMember(Kind::InterfaceMemberMethod),
          ordinal(std::move(ordinal)),
          identifier(std::move(identifier)),
          parameter_list(std::move(parameter_list)),
          maybe_response(std::move(maybe_response)) {}

    std::unique_ptr<NumericLiteral> ordinal;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<ParameterList> parameter_list;
    std::unique_ptr<Response> maybe_response;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::InterfaceMemberMethod;
    }
};

struct InterfaceBody : public Node {
    InterfaceBody(std::vector<std::unique_ptr<InterfaceMember>> fields)
        : Node(Kind::InterfaceBody),
          fields(std::move(fields)) {}

    std::vector<std::unique_ptr<InterfaceMember>> fields;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::InterfaceBody;
    }
};

struct InterfaceDeclaration : public Declaration {
    InterfaceDeclaration(std::unique_ptr<Identifier> identifier,
                         std::unique_ptr<InterfaceBody> body)
        : Declaration(Kind::InterfaceDeclaration),
          identifier(std::move(identifier)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<InterfaceBody> body;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::InterfaceDeclaration;
    }
};

struct StructMember : public Node {
    StructMember(Kind kind)
        : Node(kind) {}

    static bool isinstance(const Node* node) {
        switch (node->kind) {
        case Kind::StructMemberConst:
        case Kind::StructMemberEnum:
        case Kind::StructMemberField:
            return true;

        default:
            return false;
        }
    }
};

struct StructMemberConst : public StructMember {
    StructMemberConst(std::unique_ptr<ConstDeclaration> const_declaration)
        : StructMember(Kind::StructMemberConst),
          const_declaration(std::move(const_declaration)) {}

    std::unique_ptr<ConstDeclaration> const_declaration;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::StructMemberConst;
    }
};

struct StructMemberEnum : public StructMember {
    StructMemberEnum(std::unique_ptr<EnumDeclaration> enum_declaration)
        : StructMember(Kind::StructMemberEnum),
          enum_declaration(std::move(enum_declaration)) {}

    std::unique_ptr<EnumDeclaration> enum_declaration;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::StructMemberEnum;
    }
};

struct StructDefaultValue : public Node {
    StructDefaultValue(std::unique_ptr<Constant> const_declaration)
        : Node(Kind::StructDefaultValue),
          const_declaration(std::move(const_declaration)) {}

    std::unique_ptr<Constant> const_declaration;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::StructDefaultValue;
    }
};

struct StructMemberField : public StructMember {
    StructMemberField(std::unique_ptr<Type> type,
                      std::unique_ptr<Identifier> identifier,
                      std::unique_ptr<StructDefaultValue> maybe_default_value)
        : StructMember(Kind::StructMemberField),
          type(std::move(type)),
          identifier(std::move(identifier)),
          maybe_default_value(std::move(maybe_default_value)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<StructDefaultValue> maybe_default_value;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::StructMemberField;
    }
};

struct StructBody : public Node {
    StructBody(std::vector<std::unique_ptr<StructMember>> fields)
        : Node(Kind::StructBody),
          fields(std::move(fields)) {}

    std::vector<std::unique_ptr<StructMember>> fields;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::StructBody;
    }
};

struct StructDeclaration : public Declaration {
    StructDeclaration(std::unique_ptr<Identifier> identifier,
                      std::unique_ptr<StructBody> body)
        : Declaration(Kind::StructDeclaration),
          identifier(std::move(identifier)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<StructBody> body;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::StructDeclaration;
    }
};

struct UnionMember : public Node {
    UnionMember(std::unique_ptr<Type> type,
                std::unique_ptr<Identifier> identifier)
        : Node(Kind::UnionMember),
          type(std::move(type)),
          identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::UnionMember;
    }
};

struct UnionBody : public Node {
    UnionBody(std::vector<std::unique_ptr<UnionMember>> fields)
        : Node(Kind::UnionBody),
          fields(std::move(fields)) {}

    std::vector<std::unique_ptr<UnionMember>> fields;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::UnionBody;
    }
};

struct UnionDeclaration : public Declaration {
    UnionDeclaration(std::unique_ptr<Identifier> identifier,
                     std::unique_ptr<UnionBody> body)
        : Declaration(Kind::UnionDeclaration),
          identifier(std::move(identifier)),
          body(std::move(body)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<UnionBody> body;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::UnionDeclaration;
    }
};

struct DeclarationList : public Node {
    DeclarationList(std::vector<std::unique_ptr<Declaration>> declaration_list)
        : Node(Kind::DeclarationList),
          declaration_list(std::move(declaration_list)) {}

    std::vector<std::unique_ptr<Declaration>> declaration_list;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::DeclarationList;
    }
};

struct File : public Node {
    File(std::unique_ptr<ModuleName> maybe_module,
         std::unique_ptr<UsingList> import_list,
         std::unique_ptr<DeclarationList> declaration_list)
        : Node(Kind::File),
          maybe_module(std::move(maybe_module)),
          import_list(std::move(import_list)),
          declaration_list(std::move(declaration_list)) {}

    std::unique_ptr<ModuleName> maybe_module;
    std::unique_ptr<UsingList> import_list;
    std::unique_ptr<DeclarationList> declaration_list;

    static bool isinstance(const Node* node) {
        return node->kind == Kind::File;
    }
};

} // namespace fidl
