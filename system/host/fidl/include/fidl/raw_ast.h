// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_RAW_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_RAW_AST_H_

#include <memory>
#include <utility>
#include <vector>

#include "source_location.h"
#include "token.h"
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

// In order to be able to associate AST nodes with their original source, each
// node is a SourceElement, which contains information about the original
// source.  The AST has a start token, whose previous_end field points to the
// end of the previous AST node, and an end token, which points to the end of
// this syntactic element.
//
// Note: The file may have a tail of whitespace / comment text not explicitly
// associated with any node.  In order to reconstruct that text, raw::File
// contains an end token; the previous_end field of that token points to the end
// of the last interesting token.
struct SourceElement {
    SourceElement(Token start, Token end)
        : start_(start), end_(end) {}

    SourceLocation location() const { return start_.location(); }

    // The start_ token is the first token associated with the AST node, and
    // contains everything in the gap between the end of the previous "interesting"
    // token and the end of the first token in this AST node.  "Interesting" means
    // "not discarded by previous compiler passes", and includes whitespace,
    // comments, and things like braces / parentheses that aren't at the end of a
    // node.
    Token start_;

    // The end_ token is the end of the source for the node; often a
    // right curly brace or semicolon.  Note that these values might not be unique.
    // For example, the token that starts an identifier list is also the token for
    // the first identifier in the list.
    Token end_;
};

struct Identifier : SourceElement {
    explicit Identifier(Token token)
        : SourceElement(token, token) {}

    explicit Identifier(Token start_token, Token identifier_token)
        : SourceElement(Token(start_token.previous_end(), identifier_token.location(), identifier_token.kind()), identifier_token) {}
};

struct CompoundIdentifier : SourceElement {
    CompoundIdentifier(Token start, Token end, std::vector<std::unique_ptr<Identifier>> components)
        : SourceElement(start, end), components(std::move(components)) {}

    std::vector<std::unique_ptr<Identifier>> components;
};

struct Literal : SourceElement {
    virtual ~Literal() {}

    enum struct Kind {
        kString,
        kNumeric,
        kTrue,
        kFalse,
    };

    explicit Literal(Token token, Kind kind)
        : SourceElement(token, token), kind(kind) {}

    const Kind kind;
};

struct StringLiteral : public Literal {
    explicit StringLiteral(Token start)
        : Literal(start, Kind::kString) {}
};

struct NumericLiteral : public Literal {
    explicit NumericLiteral(Token token)
        : Literal(token, Kind::kNumeric) {}
};

struct TrueLiteral : public Literal {
    explicit TrueLiteral(Token token)
        : Literal(token, Kind::kTrue) {}
};

struct FalseLiteral : public Literal {
    explicit FalseLiteral(Token token)
        : Literal(token, Kind::kFalse) {}
};

struct Constant : SourceElement {
    virtual ~Constant() {}

    enum struct Kind {
        kIdentifier,
        kLiteral,
    };

    explicit Constant(Token token, Kind kind)
        : SourceElement(token, token), kind(kind) {}

    const Kind kind;
};

struct IdentifierConstant : Constant {
    explicit IdentifierConstant(std::unique_ptr<CompoundIdentifier> identifier)
        : Constant(identifier->start_, Kind::kIdentifier), identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;
};

struct LiteralConstant : Constant {
    explicit LiteralConstant(std::unique_ptr<Literal> literal)
        : Constant(literal->start_, Kind::kLiteral), literal(std::move(literal)) {}

    std::unique_ptr<Literal> literal;
};

struct Attribute : SourceElement {
    Attribute(Token start, Token end, std::unique_ptr<Identifier> name, std::unique_ptr<StringLiteral> value)
        : SourceElement(start, end), name(std::move(name)), value(std::move(value)) {}

    std::unique_ptr<Identifier> name;
    std::unique_ptr<StringLiteral> value;
};

struct AttributeList : SourceElement {
    AttributeList(Token start, Token end, std::vector<std::unique_ptr<Attribute>> attribute_list)
        : SourceElement(start, end), attribute_list(std::move(attribute_list)) {}

    std::vector<std::unique_ptr<Attribute>> attribute_list;

    bool HasAttribute(fidl::StringView name) const {
        for (const auto& attribute : attribute_list) {
            if (attribute->name->location().data() == name)
                return true;
        }
        return false;
    }
};

struct Type : SourceElement {
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

    explicit Type(Token start, Token end, Kind kind)
        : SourceElement(start, end), kind(kind) {}

    const Kind kind;
};

struct ArrayType : public Type {
    ArrayType(Token start, Token end, std::unique_ptr<Type> element_type, std::unique_ptr<Constant> element_count)
        : Type(start, end, Kind::kArray), element_type(std::move(element_type)),
          element_count(std::move(element_count)) {}

    std::unique_ptr<Type> element_type;
    std::unique_ptr<Constant> element_count;
};

struct VectorType : public Type {
    VectorType(Token start, Token end, std::unique_ptr<Type> element_type, std::unique_ptr<Constant> maybe_element_count,
               types::Nullability nullability)
        : Type(start, end, Kind::kVector), element_type(std::move(element_type)),
          maybe_element_count(std::move(maybe_element_count)), nullability(nullability) {}

    std::unique_ptr<Type> element_type;
    std::unique_ptr<Constant> maybe_element_count;
    types::Nullability nullability;
};

struct StringType : public Type {
    StringType(Token start, Token end, std::unique_ptr<Constant> maybe_element_count, types::Nullability nullability)
        : Type(start, end, Kind::kString), maybe_element_count(std::move(maybe_element_count)),
          nullability(nullability) {}

    std::unique_ptr<Constant> maybe_element_count;
    types::Nullability nullability;
};

struct HandleType : public Type {
    HandleType(Token start, Token end, types::HandleSubtype subtype, types::Nullability nullability)
        : Type(start, end, Kind::kHandle), subtype(subtype), nullability(nullability) {}

    types::HandleSubtype subtype;
    types::Nullability nullability;
};

struct RequestHandleType : public Type {
    RequestHandleType(Token start, Token end, std::unique_ptr<CompoundIdentifier> identifier,
                      types::Nullability nullability)
        : Type(start, end, Kind::kRequestHandle), identifier(std::move(identifier)), nullability(nullability) {}

    std::unique_ptr<CompoundIdentifier> identifier;
    types::Nullability nullability;
};

struct PrimitiveType : public Type {
    PrimitiveType(Token start, Token end, types::PrimitiveSubtype subtype)
        : Type(start, end, Kind::kPrimitive), subtype(subtype) {}

    types::PrimitiveSubtype subtype;
};

struct IdentifierType : public Type {
    IdentifierType(Token start, Token end, std::unique_ptr<CompoundIdentifier> identifier, types::Nullability nullability)
        : Type(start, end, Kind::kIdentifier), identifier(std::move(identifier)), nullability(nullability) {}

    std::unique_ptr<CompoundIdentifier> identifier;
    types::Nullability nullability;
};

struct Using : SourceElement {
    Using(Token start, Token end, std::unique_ptr<CompoundIdentifier> using_path, std::unique_ptr<Identifier> maybe_alias, std::unique_ptr<PrimitiveType> maybe_primitive)
        : SourceElement(start, end), using_path(std::move(using_path)), maybe_alias(std::move(maybe_alias)), maybe_primitive(std::move(maybe_primitive)) {}

    std::unique_ptr<CompoundIdentifier> using_path;
    std::unique_ptr<Identifier> maybe_alias;
    std::unique_ptr<PrimitiveType> maybe_primitive;
};

struct ConstDeclaration : SourceElement {
    ConstDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes, std::unique_ptr<Type> type,
                     std::unique_ptr<Identifier> identifier, std::unique_ptr<Constant> constant)
        : SourceElement(start, end), attributes(std::move(attributes)), type(std::move(type)),
          identifier(std::move(identifier)), constant(std::move(constant)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> constant;
};

struct EnumMember : SourceElement {
    EnumMember(Token end, std::unique_ptr<Identifier> identifier, std::unique_ptr<Constant> value)
        : SourceElement(identifier->start_, end), identifier(std::move(identifier)), value(std::move(value)) {}

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> value;
};

struct EnumDeclaration : SourceElement {
    EnumDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<PrimitiveType> maybe_subtype,
                    std::vector<std::unique_ptr<EnumMember>> members)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          maybe_subtype(std::move(maybe_subtype)), members(std::move(members)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<PrimitiveType> maybe_subtype;
    std::vector<std::unique_ptr<EnumMember>> members;
};

struct Parameter : SourceElement {
    Parameter(Token start, Token end, std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier)
        : SourceElement(start, end), type(std::move(type)), identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

struct ParameterList : SourceElement {
    ParameterList(Token start, Token end, std::vector<std::unique_ptr<Parameter>> parameter_list)
        : SourceElement(start, end), parameter_list(std::move(parameter_list)) {}

    std::vector<std::unique_ptr<Parameter>> parameter_list;
};

struct InterfaceMethod : SourceElement {
    InterfaceMethod(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<NumericLiteral> ordinal,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<ParameterList> maybe_request,
                    std::unique_ptr<ParameterList> maybe_response)
        : SourceElement(start, end), attributes(std::move(attributes)),
          ordinal(std::move(ordinal)), identifier(std::move(identifier)),
          maybe_request(std::move(maybe_request)), maybe_response(std::move(maybe_response)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<NumericLiteral> ordinal;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<ParameterList> maybe_request;
    std::unique_ptr<ParameterList> maybe_response;
};

struct InterfaceDeclaration : SourceElement {
    InterfaceDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                         std::unique_ptr<Identifier> identifier,
                         std::vector<std::unique_ptr<CompoundIdentifier>> superinterfaces,
                         std::vector<std::unique_ptr<InterfaceMethod>> methods)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          superinterfaces(std::move(superinterfaces)), methods(std::move(methods)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<InterfaceMethod>> methods;
};

struct StructMember : SourceElement {
    StructMember(Token end, std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier,
                 std::unique_ptr<Constant> maybe_default_value)
        : SourceElement(type->start_, end), type(std::move(type)), identifier(std::move(identifier)),
          maybe_default_value(std::move(maybe_default_value)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> maybe_default_value;
};

struct StructDeclaration : SourceElement {
    // Note: A nullptr passed to attributes means an empty attribute list.
    StructDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                      std::unique_ptr<Identifier> identifier,
                      std::vector<std::unique_ptr<StructMember>> members)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          members(std::move(members)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<StructMember>> members;
};

struct UnionMember : SourceElement {
    UnionMember(Token start, Token end, std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier)
        : SourceElement(start, end), type(std::move(type)), identifier(std::move(identifier)) {}

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

struct UnionDeclaration : SourceElement {
    UnionDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                     std::unique_ptr<Identifier> identifier,
                     std::vector<std::unique_ptr<UnionMember>> members)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          members(std::move(members)) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<UnionMember>> members;
};

struct File : SourceElement {
    File(Token start, Token end,
         std::unique_ptr<AttributeList> attributes,
         std::unique_ptr<CompoundIdentifier> library_name,
         std::vector<std::unique_ptr<Using>> using_list,
         std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list,
         std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list,
         std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list,
         std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list,
         std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list)
        : SourceElement(start, end),
          attributes(std::move(attributes)),
          library_name(std::move(library_name)),
          using_list(std::move(using_list)),
          const_declaration_list(std::move(const_declaration_list)),
          enum_declaration_list(std::move(enum_declaration_list)),
          interface_declaration_list(std::move(interface_declaration_list)),
          struct_declaration_list(std::move(struct_declaration_list)),
          union_declaration_list(std::move(union_declaration_list)),
          end_(end) {}

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<CompoundIdentifier> library_name;
    std::vector<std::unique_ptr<Using>> using_list;
    std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list;
    Token end_;
};

} // namespace raw
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_RAW_AST_H_
