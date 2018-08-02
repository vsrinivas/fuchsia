// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_RAW_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_RAW_AST_H_

#include <memory>
#include <utility>
#include <vector>

#include "source_location.h"
#include "types.h"

// ASTs fresh out of the oven. This is a tree-shaped bunch of nodes
// pretty much exactly corresponding to the grammar of a single fidl
// file. File is the root of the tree, and consists of lists of
// Declarations, and so on down to individual SourceLocations.

// Each node owns its children via unique_ptr and vector. All tokens
// here, like everywhere in the fidl compiler, are backed by a string
// view whose contents are owned by a SourceManager.

// A raw::File is produced by parsing a token stream. All of the
// Files in a library are then flattened out into a Library.

namespace fidl {
namespace raw {

struct Identifier {
    explicit Identifier(SourceLocation location)
        : location(location) {}

    SourceLocation location;
};

struct CompoundIdentifier {
    CompoundIdentifier(std::vector<std::unique_ptr<Identifier>> components)
        : components(std::move(components)) {}

    std::vector<std::unique_ptr<Identifier>> components;
};

struct Literal {
    virtual ~Literal() {}

    enum struct Kind {
        kString,
        kNumeric,
        kTrue,
        kFalse,
    };

    explicit Literal(Kind kind)
        : kind(kind) {}

    const Kind kind;
};

struct StringLiteral : public Literal {
    explicit StringLiteral(SourceLocation location)
        : Literal(Kind::kString), location(location) {}

    SourceLocation location;
};

struct NumericLiteral : public Literal {
    NumericLiteral(SourceLocation location)
        : Literal(Kind::kNumeric), location(location) {}

    SourceLocation location;
};

struct TrueLiteral : public Literal {
    TrueLiteral()
        : Literal(Kind::kTrue) {}
};

struct FalseLiteral : public Literal {
    FalseLiteral()
        : Literal(Kind::kFalse) {}
};

struct Constant {
    virtual ~Constant() {}

    enum struct Kind {
        kIdentifier,
        kLiteral,
    };

    explicit Constant(Kind kind)
        : kind(kind) {}

    const Kind kind;
};

struct IdentifierConstant : Constant {
    explicit IdentifierConstant(std::unique_ptr<CompoundIdentifier> identifier)
        : Constant(Kind::kIdentifier), identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;
};

struct LiteralConstant : Constant {
    explicit LiteralConstant(std::unique_ptr<Literal> literal)
        : Constant(Kind::kLiteral), literal(std::move(literal)) {}

    std::unique_ptr<Literal> literal;
};

struct Attribute {
    Attribute(std::unique_ptr<Identifier> name, std::unique_ptr<StringLiteral> value)
        : name(std::move(name)), value(std::move(value)) {}

    std::unique_ptr<Identifier> name;
    std::unique_ptr<StringLiteral> value;
};

struct AttributeList {
    AttributeList(std::vector<std::unique_ptr<Attribute>> attribute_list)
        : attribute_list(std::move(attribute_list)) {}

    std::vector<std::unique_ptr<Attribute>> attribute_list;

    bool HasAttribute(fidl::StringView name) const {
        for (const auto& attribute : attribute_list) {
            if (attribute->name->location.data() == name)
                return true;
        }
        return false;
    }
};

struct Type {
    virtual ~Type() {}

    enum struct Kind {
        kArray,
        kVector,
        kString,
        kHandle,
        kRequestHandle,
        kPrimitive,
        kIdentifier,
    };

    explicit Type(Kind kind)
        : kind(kind) {}

    const Kind kind;
};

struct ArrayType : public Type {
    ArrayType(std::unique_ptr<Type> element_type, std::unique_ptr<Constant> element_count)
        : Type(Kind::kArray), element_type(std::move(element_type)),
          element_count(std::move(element_count)) {}

    std::unique_ptr<Type> element_type;
    std::unique_ptr<Constant> element_count;
};

struct VectorType : public Type {
    VectorType(std::unique_ptr<Type> element_type, std::unique_ptr<Constant> maybe_element_count,
               types::Nullability nullability)
        : Type(Kind::kVector), element_type(std::move(element_type)),
          maybe_element_count(std::move(maybe_element_count)), nullability(nullability) {}

    std::unique_ptr<Type> element_type;
    std::unique_ptr<Constant> maybe_element_count;
    types::Nullability nullability;
};

struct StringType : public Type {
    StringType(std::unique_ptr<Constant> maybe_element_count, types::Nullability nullability)
        : Type(Kind::kString), maybe_element_count(std::move(maybe_element_count)),
          nullability(nullability) {}

    std::unique_ptr<Constant> maybe_element_count;
    types::Nullability nullability;
};

struct HandleType : public Type {
    HandleType(types::HandleSubtype subtype, types::Nullability nullability)
        : Type(Kind::kHandle), subtype(subtype), nullability(nullability) {}

    types::HandleSubtype subtype;
    types::Nullability nullability;
};

struct RequestHandleType : public Type {
    RequestHandleType(std::unique_ptr<CompoundIdentifier> identifier,
                      types::Nullability nullability)
        : Type(Kind::kRequestHandle), identifier(std::move(identifier)), nullability(nullability) {}

    std::unique_ptr<CompoundIdentifier> identifier;
    types::Nullability nullability;
};

struct PrimitiveType : public Type {
    explicit PrimitiveType(types::PrimitiveSubtype subtype)
        : Type(Kind::kPrimitive), subtype(subtype) {}

    types::PrimitiveSubtype subtype;
};

struct IdentifierType : public Type {
    IdentifierType(std::unique_ptr<CompoundIdentifier> identifier, types::Nullability nullability)
        : Type(Kind::kIdentifier), identifier(std::move(identifier)), nullability(nullability) {}

    std::unique_ptr<CompoundIdentifier> identifier;
    types::Nullability nullability;
};

struct Using {
    Using(std::unique_ptr<CompoundIdentifier> using_path, std::unique_ptr<Identifier> maybe_alias, std::unique_ptr<PrimitiveType> maybe_primitive)
        : using_path(std::move(using_path)), maybe_alias(std::move(maybe_alias)), maybe_primitive(std::move(maybe_primitive)) {}

    std::unique_ptr<CompoundIdentifier> using_path;
    std::unique_ptr<Identifier> maybe_alias;
    std::unique_ptr<PrimitiveType> maybe_primitive;
};

struct ConstDeclaration {
    ConstDeclaration(std::unique_ptr<AttributeList> attributes, std::unique_ptr<Type> type,
                     std::unique_ptr<Identifier> identifier, std::unique_ptr<Constant> constant)
        : attributes(std::move(attributes)), type(std::move(type)),
          identifier(std::move(identifier)), constant(std::move(constant)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> constant;
};

struct EnumMember {
    EnumMember(std::unique_ptr<Identifier> identifier, std::unique_ptr<Constant> value)
        : identifier(std::move(identifier)), value(std::move(value)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> value;
};

struct EnumDeclaration {
    EnumDeclaration(std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<PrimitiveType> maybe_subtype,
                    std::vector<std::unique_ptr<EnumMember>> members)
        : attributes(std::move(attributes)), identifier(std::move(identifier)),
          maybe_subtype(std::move(maybe_subtype)), members(std::move(members)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<PrimitiveType> maybe_subtype;
    std::vector<std::unique_ptr<EnumMember>> members;
};

struct Parameter {
    Parameter(std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier)
        : type(std::move(type)), identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

struct ParameterList {
    ParameterList(std::vector<std::unique_ptr<Parameter>> parameter_list)
        : parameter_list(std::move(parameter_list)) {}

    std::vector<std::unique_ptr<Parameter>> parameter_list;
};

struct InterfaceMethod {
    InterfaceMethod(std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<NumericLiteral> ordinal,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<ParameterList> maybe_request,
                    std::unique_ptr<ParameterList> maybe_response)
        : attributes(std::move(attributes)),
          ordinal(std::move(ordinal)), identifier(std::move(identifier)),
          maybe_request(std::move(maybe_request)), maybe_response(std::move(maybe_response)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<NumericLiteral> ordinal;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<ParameterList> maybe_request;
    std::unique_ptr<ParameterList> maybe_response;
};

struct InterfaceDeclaration {
    InterfaceDeclaration(std::unique_ptr<AttributeList> attributes,
                         std::unique_ptr<Identifier> identifier,
                         std::vector<std::unique_ptr<CompoundIdentifier>> superinterfaces,
                         std::vector<std::unique_ptr<InterfaceMethod>> methods)
        : attributes(std::move(attributes)), identifier(std::move(identifier)),
          superinterfaces(std::move(superinterfaces)), methods(std::move(methods)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<InterfaceMethod>> methods;
};

struct StructMember {
    StructMember(std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier,
                 std::unique_ptr<Constant> maybe_default_value)
        : type(std::move(type)), identifier(std::move(identifier)),
          maybe_default_value(std::move(maybe_default_value)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> maybe_default_value;
};

struct StructDeclaration {
    StructDeclaration(std::unique_ptr<AttributeList> attributes,
                      std::unique_ptr<Identifier> identifier,
                      std::vector<std::unique_ptr<StructMember>> members)
        : attributes(std::move(attributes)), identifier(std::move(identifier)),
          members(std::move(members)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<StructMember>> members;
};

struct UnionMember {
    UnionMember(std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier)
        : type(std::move(type)), identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

struct UnionDeclaration {
    UnionDeclaration(std::unique_ptr<AttributeList> attributes,
                     std::unique_ptr<Identifier> identifier,
                     std::vector<std::unique_ptr<UnionMember>> members)
        : attributes(std::move(attributes)), identifier(std::move(identifier)),
          members(std::move(members)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<UnionMember>> members;
};

struct File {
    File(std::unique_ptr<AttributeList> attributes,
         std::unique_ptr<CompoundIdentifier> library_name,
         std::vector<std::unique_ptr<Using>> using_list,
         std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list,
         std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list,
         std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list,
         std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list,
         std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list)
        : attributes(std::move(attributes)),
          library_name(std::move(library_name)),
          using_list(std::move(using_list)),
          const_declaration_list(std::move(const_declaration_list)),
          enum_declaration_list(std::move(enum_declaration_list)),
          interface_declaration_list(std::move(interface_declaration_list)),
          struct_declaration_list(std::move(struct_declaration_list)),
          union_declaration_list(std::move(union_declaration_list)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<CompoundIdentifier> library_name;
    std::vector<std::unique_ptr<Using>> using_list;
    std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list;
};

} // namespace raw
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_RAW_AST_H_
