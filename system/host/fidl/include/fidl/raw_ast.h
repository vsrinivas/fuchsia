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

// This class has a tight coupling with the TreeVisitor class.  Each node has a
// corresponding method in that class.  Each node type also has an Accept()
// method to help visitors visit the node.  When you add a new node, or add a
// field to an existing node, you must ensure the Accept method works.

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
class TreeVisitor;

class SourceElement {
public:
    explicit SourceElement(Token start, Token end)
        : start_(start), end_(end) {}

    SourceLocation location() const { return start_.location(); }

    virtual ~SourceElement() {}

    Token start_;
    Token end_;
};

class SourceElementMark {
public:
    SourceElementMark(TreeVisitor& tv,
                      const SourceElement& element);

    ~SourceElementMark();

private:
    TreeVisitor& tv_;
    const SourceElement& element_;
};

class Identifier : public SourceElement {
public:
    explicit Identifier(Token token)
        : SourceElement(token, token) {}

    explicit Identifier(Token start_token, Token identifier_token)
        : SourceElement(Token(start_token.previous_end(), identifier_token.location(), identifier_token.kind()), identifier_token) {}

    virtual ~Identifier() {}

    void Accept(TreeVisitor& visitor) {
        SourceElementMark sem(visitor, *this);
    }
};

class CompoundIdentifier : public SourceElement {
public:
    CompoundIdentifier(Token start, Token end, std::vector<std::unique_ptr<Identifier>> components)
        : SourceElement(start, end), components(std::move(components)) {}


    virtual ~CompoundIdentifier() {}

    std::vector<std::unique_ptr<Identifier>> components;

    void Accept(TreeVisitor& visitor);
};

class Literal : public SourceElement {
public:
    enum struct Kind {
        kString,
        kNumeric,
        kTrue,
        kFalse,
    };

    explicit Literal(Token token, Kind kind)
        : SourceElement(token, token), kind(kind) {}

    virtual ~Literal() {}

    const Kind kind;
};

class StringLiteral : public Literal {
public:
    explicit StringLiteral(Token start)
        : Literal(start, Kind::kString) {}

    void Accept(TreeVisitor& visitor);
};

class NumericLiteral : public Literal {
public:
    NumericLiteral(Token token)
        : Literal(token, Kind::kNumeric) {}

    void Accept(TreeVisitor& visitor);
};

class TrueLiteral : public Literal {
public:
    TrueLiteral(Token token)
        : Literal(token, Kind::kTrue) {}

    void Accept(TreeVisitor& visitor);
};

class FalseLiteral : public Literal {
public:
    FalseLiteral(Token token)
        : Literal(token, Kind::kFalse) {}

    void Accept(TreeVisitor& visitor);
};

class Constant : public SourceElement {
public:
    enum class Kind {
        kIdentifier,
        kLiteral,
    };

    explicit Constant(Token token, Kind kind)
        : SourceElement(token, token), kind(kind) {}

    virtual ~Constant() {}

    const Kind kind;
};

class IdentifierConstant : public Constant {
public:
    explicit IdentifierConstant(std::unique_ptr<CompoundIdentifier> identifier)
        : Constant(identifier->start_, Kind::kIdentifier), identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;

    void Accept(TreeVisitor& visitor);
};

class LiteralConstant : public Constant {
public:
    explicit LiteralConstant(std::unique_ptr<Literal> literal)
        : Constant(literal->start_, Kind::kLiteral), literal(std::move(literal)) {}

    std::unique_ptr<Literal> literal;

    void Accept(TreeVisitor& visitor);
};

class Attribute : public SourceElement {
public:
    Attribute(Token start, Token end, std::string name, std::string value)
        : SourceElement(start, end), name(std::move(name)), value(std::move(value)) {}

    void Accept(TreeVisitor& visitor);

    const std::string name;
    const std::string value;
};

class Attributes {
public:
    bool Insert(std::unique_ptr<raw::Attribute> attribute) {
        if (HasAttribute(StringView(attribute->name))) {
            return false;
        }
        attributes_.push_back(std::move(attribute));
        return true;
    }

    bool HasAttribute(std::string name) const {
        for (const auto& attribute : attributes_) {
            if (attribute->name == name)
                return true;
        }
        return false;
    }

    std::vector<std::unique_ptr<Attribute>> attributes_;
};

class AttributeList : public SourceElement {
public:
    AttributeList(Token start, Token end, std::unique_ptr<Attributes> attributes)
        : SourceElement(start, end), attributes_(std::move(attributes)) {}

    bool HasAttribute(fidl::StringView name) const {
        return attributes_->HasAttribute(StringView(name));
    }

    bool Insert(std::unique_ptr<raw::Attribute> attribute) {
        return attributes_->Insert(std::move(attribute));
    }

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<Attributes> attributes_;
};

class Type : public SourceElement {
public:
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

class ArrayType : public Type {
public:
    ArrayType(Token start, Token end, std::unique_ptr<Type> element_type, std::unique_ptr<Constant> element_count)
        : Type(start, end, Kind::kArray), element_type(std::move(element_type)),
          element_count(std::move(element_count)) {}

    void Accept(TreeVisitor& visitor);
    std::unique_ptr<Type> element_type;
    std::unique_ptr<Constant> element_count;
};

class VectorType : public Type {
public:
    VectorType(Token start, Token end, std::unique_ptr<Type> element_type, std::unique_ptr<Constant> maybe_element_count,
               types::Nullability nullability)
        : Type(start, end, Kind::kVector), element_type(std::move(element_type)),
          maybe_element_count(std::move(maybe_element_count)), nullability(nullability) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<Type> element_type;
    std::unique_ptr<Constant> maybe_element_count;
    types::Nullability nullability;
};

class StringType : public Type {
public:
    StringType(Token start, Token end, std::unique_ptr<Constant> maybe_element_count, types::Nullability nullability)
        : Type(start, end, Kind::kString), maybe_element_count(std::move(maybe_element_count)),
          nullability(nullability) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<Constant> maybe_element_count;
    types::Nullability nullability;
};

class HandleType : public Type {
public:
    HandleType(Token start, Token end, types::HandleSubtype subtype, types::Nullability nullability)
        : Type(start, end, Kind::kHandle), subtype(subtype), nullability(nullability) {}

    void Accept(TreeVisitor& visitor);

    types::HandleSubtype subtype;
    types::Nullability nullability;
};

class RequestHandleType : public Type {
public:
    RequestHandleType(Token start, Token end, std::unique_ptr<CompoundIdentifier> identifier,
                      types::Nullability nullability)
        : Type(start, end, Kind::kRequestHandle), identifier(std::move(identifier)), nullability(nullability) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<CompoundIdentifier> identifier;
    types::Nullability nullability;
};

class PrimitiveType : public Type {
public:
    explicit PrimitiveType(Token start, Token end, types::PrimitiveSubtype subtype)
        : Type(start, end, Kind::kPrimitive), subtype(subtype) {}

    void Accept(TreeVisitor& visitor);

    types::PrimitiveSubtype subtype;
};

class IdentifierType : public Type {
public:
    IdentifierType(Token start, Token end, std::unique_ptr<CompoundIdentifier> identifier, types::Nullability nullability)
        : Type(start, end, Kind::kIdentifier), identifier(std::move(identifier)), nullability(nullability) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<CompoundIdentifier> identifier;
    types::Nullability nullability;
};

class Using : public SourceElement {
public:
    Using(Token start, Token end, std::unique_ptr<CompoundIdentifier> using_path, std::unique_ptr<Identifier> maybe_alias, std::unique_ptr<PrimitiveType> maybe_primitive)
        : SourceElement(start, end), using_path(std::move(using_path)), maybe_alias(std::move(maybe_alias)), maybe_primitive(std::move(maybe_primitive)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<CompoundIdentifier> using_path;
    std::unique_ptr<Identifier> maybe_alias;
    // TODO(pascal): We should be more explicit for type aliases such as
    // `using foo = int8;` and use a special purpose AST element.
    std::unique_ptr<PrimitiveType> maybe_primitive;
};

class ConstDeclaration : public SourceElement {
public:
    ConstDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes, std::unique_ptr<Type> type,
                     std::unique_ptr<Identifier> identifier, std::unique_ptr<Constant> constant)
        : SourceElement(start, end), attributes(std::move(attributes)), type(std::move(type)),
          identifier(std::move(identifier)), constant(std::move(constant)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> constant;
};

class EnumMember : public SourceElement {
public:
    EnumMember(Token end, std::unique_ptr<Identifier> identifier, std::unique_ptr<Constant> value, std::unique_ptr<AttributeList> attributes)
        : SourceElement(identifier->start_, end), identifier(std::move(identifier)), value(std::move(value)), attributes(std::move(attributes)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> value;
    std::unique_ptr<AttributeList> attributes;
};

class EnumDeclaration : public SourceElement {
public:
    EnumDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<PrimitiveType> maybe_subtype,
                    std::vector<std::unique_ptr<EnumMember>> members)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          maybe_subtype(std::move(maybe_subtype)), members(std::move(members)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<PrimitiveType> maybe_subtype;
    std::vector<std::unique_ptr<EnumMember>> members;
};

class Parameter : public SourceElement {
public:
    Parameter(Token start, Token end, std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier)
        : SourceElement(start, end), type(std::move(type)), identifier(std::move(identifier)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
};

class ParameterList : public SourceElement {
public:
    ParameterList(Token start, Token end, std::vector<std::unique_ptr<Parameter>> parameter_list)
        : SourceElement(start, end), parameter_list(std::move(parameter_list)) {}

    void Accept(TreeVisitor& visitor);

    std::vector<std::unique_ptr<Parameter>> parameter_list;
};

class InterfaceMethod : public SourceElement {
public:
    InterfaceMethod(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<NumericLiteral> ordinal,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<ParameterList> maybe_request,
                    std::unique_ptr<ParameterList> maybe_response)
        : SourceElement(start, end), attributes(std::move(attributes)),
          ordinal(std::move(ordinal)), identifier(std::move(identifier)),
          maybe_request(std::move(maybe_request)), maybe_response(std::move(maybe_response)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<NumericLiteral> ordinal;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<ParameterList> maybe_request;
    std::unique_ptr<ParameterList> maybe_response;
};

class InterfaceDeclaration : public SourceElement {
public:
    InterfaceDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                         std::unique_ptr<Identifier> identifier,
                         std::vector<std::unique_ptr<CompoundIdentifier>> superinterfaces,
                         std::vector<std::unique_ptr<InterfaceMethod>> methods)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          superinterfaces(std::move(superinterfaces)), methods(std::move(methods)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<CompoundIdentifier>> superinterfaces;
    std::vector<std::unique_ptr<InterfaceMethod>> methods;
};

class StructMember : public SourceElement {
public:
    StructMember(Token end, std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier,
                 std::unique_ptr<Constant> maybe_default_value,
                 std::unique_ptr<AttributeList> attributes)
        : SourceElement(type->start_, end), type(std::move(type)), identifier(std::move(identifier)),
          maybe_default_value(std::move(maybe_default_value)), attributes(std::move(attributes)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> maybe_default_value;
    std::unique_ptr<AttributeList> attributes;
};

class StructDeclaration : public SourceElement {
public:
    // Note: A nullptr passed to attributes means an empty attribute list.
    StructDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                      std::unique_ptr<Identifier> identifier,
                      std::vector<std::unique_ptr<StructMember>> members)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          members(std::move(members)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<StructMember>> members;
};

class UnionMember : public SourceElement {
public:
    UnionMember(Token start, Token end, std::unique_ptr<Type> type, std::unique_ptr<Identifier> identifier,
                std::unique_ptr<AttributeList> attributes)
        : SourceElement(start, end), type(std::move(type)), identifier(std::move(identifier)), attributes(std::move(attributes)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<Type> type;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<AttributeList> attributes;
};

class UnionDeclaration : public SourceElement {
public:
    UnionDeclaration(Token start, Token end, std::unique_ptr<AttributeList> attributes,
                     std::unique_ptr<Identifier> identifier,
                     std::vector<std::unique_ptr<UnionMember>> members)
        : SourceElement(start, end), attributes(std::move(attributes)), identifier(std::move(identifier)),
          members(std::move(members)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<UnionMember>> members;
};

class File : public SourceElement {
public:
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

    void Accept(TreeVisitor& visitor);

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
