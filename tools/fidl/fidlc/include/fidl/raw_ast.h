// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_RAW_AST_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_RAW_AST_H_

#include <zircon/assert.h>

#include <memory>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "tools/fidl/fidlc/include/fidl/source_span.h"
#include "tools/fidl/fidlc/include/fidl/token.h"
#include "tools/fidl/fidlc/include/fidl/types.h"
#include "tools/fidl/fidlc/include/fidl/utils.h"

// ASTs fresh out of the oven. This is a tree-shaped bunch of nodes
// pretty much exactly corresponding to the grammar of a single fidl
// file. File is the root of the tree, and consists of lists of
// Declarations, and so on down to individual SourceSpans.
// See
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#compiler_internals
// for additional context

// Each node owns its children via unique_ptr and vector. All tokens
// here, like everywhere in the fidl compiler, are backed by a string
// view whose contents are owned by a SourceManager.

// This class has a tight coupling with the TreeVisitor class.  Each node has a
// corresponding method in that class.  Each node type also has an Accept()
// method to help visitors visit the node.  When you add a new node, or add a
// field to an existing node, you must ensure the Accept method works.

// A raw::File is produced by parsing a token stream. All of the
// Files in a library are then flattened out into a Library.

namespace fidl::raw {

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
  explicit SourceElement(Token start, Token end) : start_(start), end_(end) {}
  virtual ~SourceElement() = default;

  bool has_span() const {
    return start_.span().valid() && end_.span().valid() &&
           &start_.span().source_file() == &end_.span().source_file();
  }

  SourceSpan span() const {
    if (!start_.span().valid() || !end_.span().valid()) {
      return SourceSpan();
    }

    ZX_ASSERT(has_span());
    const char* start_pos = start_.span().data().data();
    const char* end_pos = end_.span().data().data() + end_.span().data().length();
    return SourceSpan(std::string_view(start_pos, end_pos - start_pos),
                      start_.span().source_file());
  }

  std::string copy_to_str() const {
    const char* start_pos = start_.span().data().data();
    const char* end_pos = end_.span().data().data() + end_.span().data().length();
    return std::string(start_pos, end_pos);
  }

  void update_span(SourceElement const& element) {
    start_ = element.start_;
    end_ = element.end_;
  }

  Token start_;
  Token end_;
};

class SourceElementMark {
 public:
  SourceElementMark(TreeVisitor* tv, const SourceElement& element);

  ~SourceElementMark();

 private:
  TreeVisitor* tv_;
  const SourceElement& element_;
};

class Identifier final : public SourceElement {
 public:
  explicit Identifier(SourceElement const& element) : SourceElement(element) {}

  void Accept(TreeVisitor* visitor) const;
};

class CompoundIdentifier final : public SourceElement {
 public:
  CompoundIdentifier(SourceElement const& element,
                     std::vector<std::unique_ptr<Identifier>> components)
      : SourceElement(element), components(std::move(components)) {}

  void Accept(TreeVisitor* visitor) const;

  std::vector<std::unique_ptr<Identifier>> components;
};

class Literal : public SourceElement {
 public:
  enum struct Kind {
    kDocComment,
    kString,
    kNumeric,
    kBool,
  };

  explicit Literal(SourceElement const& element, Kind kind) : SourceElement(element), kind(kind) {}

  const Kind kind;
};

class DocCommentLiteral final : public Literal {
 public:
  explicit DocCommentLiteral(SourceElement const& element) : Literal(element, Kind::kDocComment) {}

  void Accept(TreeVisitor* visitor) const;

  std::string MakeContents() const {
    if (!has_span() || span().data().empty()) {
      return "";
    }
    return fidl::utils::strip_doc_comment_slashes(span().data());
  }
};

class StringLiteral final : public Literal {
 public:
  explicit StringLiteral(SourceElement const& element) : Literal(element, Kind::kString) {}

  void Accept(TreeVisitor* visitor) const;

  std::string MakeContents() const {
    if (!has_span() || span().data().empty()) {
      return "";
    }
    return fidl::utils::strip_string_literal_quotes(span().data());
  }
};

class NumericLiteral final : public Literal {
 public:
  explicit NumericLiteral(SourceElement const& element) : Literal(element, Kind::kNumeric) {}

  void Accept(TreeVisitor* visitor) const;
};

class Ordinal64 final : public SourceElement {
 public:
  Ordinal64(SourceElement const& element, uint64_t value) : SourceElement(element), value(value) {}

  void Accept(TreeVisitor* visitor) const;

  const uint64_t value;
};

class BoolLiteral final : public Literal {
 public:
  BoolLiteral(SourceElement const& element, bool value)
      : Literal(element, Kind::kBool), value(value) {}

  void Accept(TreeVisitor* visitor) const;

  const bool value;
};

class Constant : public SourceElement {
 public:
  enum class Kind { kIdentifier, kLiteral, kBinaryOperator };

  explicit Constant(Token token, Kind kind) : SourceElement(token, token), kind(kind) {}
  explicit Constant(const SourceElement& element, Kind kind) : SourceElement(element), kind(kind) {}

  const Kind kind;
};

class IdentifierConstant final : public Constant {
 public:
  explicit IdentifierConstant(std::unique_ptr<CompoundIdentifier> identifier)
      : Constant(SourceElement(identifier->start_, identifier->end_), Kind::kIdentifier),
        identifier(std::move(identifier)) {}

  std::unique_ptr<CompoundIdentifier> identifier;

  void Accept(TreeVisitor* visitor) const;
};

class LiteralConstant final : public Constant {
 public:
  explicit LiteralConstant(std::unique_ptr<Literal> literal)
      : Constant(literal->start_, Kind::kLiteral), literal(std::move(literal)) {}

  std::unique_ptr<Literal> literal;

  void Accept(TreeVisitor* visitor) const;
};

class BinaryOperatorConstant final : public Constant {
 public:
  enum class Operator { kOr };
  explicit BinaryOperatorConstant(std::unique_ptr<Constant> left_operand,
                                  std::unique_ptr<Constant> right_operand, Operator op)
      : Constant(SourceElement(left_operand->start_, right_operand->end_), Kind::kBinaryOperator),
        left_operand(std::move(left_operand)),
        right_operand(std::move(right_operand)),
        op(op) {}

  std::unique_ptr<Constant> left_operand;
  std::unique_ptr<Constant> right_operand;
  Operator op;

  void Accept(TreeVisitor* visitor) const;
};

class AttributeArg final : public SourceElement {
 public:
  // Constructor for cases where the arg name has been explicitly defined in the text.
  AttributeArg(SourceElement const& element, std::unique_ptr<Identifier> name,
               std::unique_ptr<Constant> value)
      : SourceElement(element), maybe_name(std::move(name)), value(std::move(value)) {}

  // Constructor for cases where the arg name is inferred.
  AttributeArg(SourceElement const& element, std::unique_ptr<Constant> value)
      : SourceElement(element), maybe_name(nullptr), value(std::move(value)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<Identifier> maybe_name;
  std::unique_ptr<Constant> value;
};

class Attribute final : public SourceElement {
 public:
  enum Provenance {
    kDefault,
    kDocComment,
  };

  // Constructor for cases where the name of the attribute is explicitly defined in the text.
  Attribute(SourceElement const& element, std::unique_ptr<Identifier> maybe_name,
            std::vector<std::unique_ptr<AttributeArg>> args)
      : SourceElement(element), maybe_name(std::move(maybe_name)), args(std::move(args)) {}

  // Factory for "///"-style doc comments, which have no attribute name.
  static Attribute CreateDocComment(SourceElement const& element,
                                    std::vector<std::unique_ptr<AttributeArg>> args) {
    auto attr = Attribute(element, nullptr, std::move(args));
    attr.provenance = kDocComment;
    return attr;
  }

  void Accept(TreeVisitor* visitor) const;

  Provenance provenance = Provenance::kDefault;
  std::unique_ptr<Identifier> maybe_name;
  std::vector<std::unique_ptr<AttributeArg>> args;
};

// In the raw AST, "no attributes" is represented by a null AttributeList*,
// because every SourceElement must have a valid span. (In the flat AST, it is
// the opposite: never null, but the vector can be empty.)
class AttributeList final : public SourceElement {
 public:
  AttributeList(SourceElement const& element, std::vector<std::unique_ptr<Attribute>> attributes)
      : SourceElement(element), attributes(std::move(attributes)) {}

  void Accept(TreeVisitor* visitor) const;

  std::vector<std::unique_ptr<Attribute>> attributes;
};

class TypeConstructor;

class LayoutReference;
class LayoutParameterList;
class TypeConstraints;

// The monostate variant is used to represent a parse failure.
using ConstraintOrSubtype = std::variant<std::unique_ptr<TypeConstraints>,
                                         std::unique_ptr<TypeConstructor>, std::monostate>;

class TypeConstructor final : public SourceElement {
 public:
  TypeConstructor(SourceElement const& element, std::unique_ptr<LayoutReference> layout_ref,
                  std::unique_ptr<LayoutParameterList> parameters,
                  std::unique_ptr<TypeConstraints> constraints)
      : SourceElement(element),
        layout_ref(std::move(layout_ref)),
        parameters(std::move(parameters)),
        constraints(std::move(constraints)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<LayoutReference> layout_ref;
  std::unique_ptr<LayoutParameterList> parameters;
  std::unique_ptr<TypeConstraints> constraints;
};

class AliasDeclaration final : public SourceElement {
 public:
  AliasDeclaration(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
                   std::unique_ptr<Identifier> alias, std::unique_ptr<TypeConstructor> type_ctor)
      : SourceElement(element),
        attributes(std::move(attributes)),
        alias(std::move(alias)),
        type_ctor(std::move(type_ctor)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Identifier> alias;
  std::unique_ptr<TypeConstructor> type_ctor;
};

class LibraryDecl final : public SourceElement {
 public:
  LibraryDecl(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
              std::unique_ptr<CompoundIdentifier> path)
      : SourceElement(element), attributes(std::move(attributes)), path(std::move(path)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<CompoundIdentifier> path;
};

class Using final : public SourceElement {
 public:
  Using(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
        std::unique_ptr<CompoundIdentifier> using_path, std::unique_ptr<Identifier> maybe_alias)
      : SourceElement(element),
        attributes(std::move(attributes)),
        using_path(std::move(using_path)),
        maybe_alias(std::move(maybe_alias)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<CompoundIdentifier> using_path;
  std::unique_ptr<Identifier> maybe_alias;
};

class ConstDeclaration final : public SourceElement {
 public:
  ConstDeclaration(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
                   std::unique_ptr<TypeConstructor> type_ctor,
                   std::unique_ptr<Identifier> identifier, std::unique_ptr<Constant> constant)
      : SourceElement(element),
        attributes(std::move(attributes)),
        type_ctor(std::move(type_ctor)),
        identifier(std::move(identifier)),
        constant(std::move(constant)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<TypeConstructor> type_ctor;
  std::unique_ptr<Identifier> identifier;
  std::unique_ptr<Constant> constant;
};

// A single modifier applied to a layout, protocol, or method.
template <typename T>
class Modifier final {
 public:
  Modifier(T value, Token token) : value(value), token(token) {}

  // Value of the modifier
  T value;
  // Token that the modifier is from.
  Token token;
};

class Modifiers final : public SourceElement {
 public:
  // Constructor for Layouts (has resourceness and strictness, but not openness).
  Modifiers(SourceElement const& element,
            std::optional<Modifier<types::Resourceness>> maybe_resourceness,
            std::optional<Modifier<types::Strictness>> maybe_strictness,
            bool resourceness_comes_first)
      : SourceElement(element),
        maybe_resourceness(maybe_resourceness),
        maybe_strictness(maybe_strictness),
        maybe_openness(std::nullopt),
        resourceness_comes_first(resourceness_comes_first) {}

  // Constructor for Protocols (only has openness).
  Modifiers(SourceElement const& element, std::optional<Modifier<types::Openness>> maybe_openness)
      : SourceElement(element),
        maybe_resourceness(std::nullopt),
        maybe_strictness(std::nullopt),
        maybe_openness(maybe_openness),
        resourceness_comes_first(false) {}

  // Constructor for Protocol methods (only has strictness).
  Modifiers(SourceElement const& element,
            std::optional<Modifier<types::Strictness>> maybe_strictness)
      : SourceElement(element),
        maybe_resourceness(std::nullopt),
        maybe_strictness(maybe_strictness),
        maybe_openness(std::nullopt),
        resourceness_comes_first(false) {}

  void Accept(TreeVisitor* visitor) const;

  std::optional<Modifier<types::Resourceness>> maybe_resourceness;
  std::optional<Modifier<types::Strictness>> maybe_strictness;
  std::optional<Modifier<types::Openness>> maybe_openness;
  // Whether the resourceness modifier for a layout was before the strictness
  // modifier, used for linting.
  bool resourceness_comes_first;
};

class ParameterList final : public SourceElement {
 public:
  ParameterList(SourceElement const& element, std::unique_ptr<TypeConstructor> type_ctor)
      : SourceElement(element), type_ctor(std::move(type_ctor)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<TypeConstructor> type_ctor;
};

class ProtocolMethod : public SourceElement {
 public:
  ProtocolMethod(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
                 std::unique_ptr<Modifiers> modifiers, std::unique_ptr<Identifier> identifier,
                 std::unique_ptr<ParameterList> maybe_request,
                 std::unique_ptr<ParameterList> maybe_response,
                 std::unique_ptr<TypeConstructor> maybe_error_ctor)
      : SourceElement(element),
        attributes(std::move(attributes)),
        modifiers(std::move(modifiers)),
        identifier(std::move(identifier)),
        maybe_request(std::move(maybe_request)),
        maybe_response(std::move(maybe_response)),
        maybe_error_ctor(std::move(maybe_error_ctor)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Modifiers> modifiers;
  std::unique_ptr<Identifier> identifier;
  std::unique_ptr<ParameterList> maybe_request;
  std::unique_ptr<ParameterList> maybe_response;
  std::unique_ptr<TypeConstructor> maybe_error_ctor;
};

class ProtocolCompose final : public SourceElement {
 public:
  ProtocolCompose(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
                  std::unique_ptr<CompoundIdentifier> protocol_name)
      : SourceElement(element),
        attributes(std::move(attributes)),
        protocol_name(std::move(protocol_name)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<CompoundIdentifier> protocol_name;
};

class ProtocolDeclaration final : public SourceElement {
 public:
  ProtocolDeclaration(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
                      std::unique_ptr<Modifiers> modifiers, std::unique_ptr<Identifier> identifier,
                      std::vector<std::unique_ptr<ProtocolCompose>> composed_protocols,
                      std::vector<std::unique_ptr<ProtocolMethod>> methods)
      : SourceElement(element),
        attributes(std::move(attributes)),
        modifiers(std::move(modifiers)),
        identifier(std::move(identifier)),
        composed_protocols(std::move(composed_protocols)),
        methods(std::move(methods)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Modifiers> modifiers;
  std::unique_ptr<Identifier> identifier;
  std::vector<std::unique_ptr<ProtocolCompose>> composed_protocols;
  std::vector<std::unique_ptr<ProtocolMethod>> methods;
};

class ResourceProperty final : public SourceElement {
 public:
  ResourceProperty(SourceElement const& element, std::unique_ptr<TypeConstructor> type_ctor,
                   std::unique_ptr<Identifier> identifier,
                   std::unique_ptr<AttributeList> attributes)
      : SourceElement(element),
        type_ctor(std::move(type_ctor)),
        identifier(std::move(identifier)),
        attributes(std::move(attributes)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<TypeConstructor> type_ctor;
  std::unique_ptr<Identifier> identifier;
  std::unique_ptr<AttributeList> attributes;
};

class ResourceDeclaration final : public SourceElement {
 public:
  ResourceDeclaration(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
                      std::unique_ptr<Identifier> identifier,
                      std::unique_ptr<TypeConstructor> maybe_type_ctor,
                      std::vector<std::unique_ptr<ResourceProperty>> properties)
      : SourceElement(element),
        attributes(std::move(attributes)),
        identifier(std::move(identifier)),
        maybe_type_ctor(std::move(maybe_type_ctor)),
        properties(std::move(properties)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Identifier> identifier;
  std::unique_ptr<TypeConstructor> maybe_type_ctor;
  std::vector<std::unique_ptr<ResourceProperty>> properties;
};

class ServiceMember final : public SourceElement {
 public:
  ServiceMember(SourceElement const& element, std::unique_ptr<TypeConstructor> type_ctor,
                std::unique_ptr<Identifier> identifier, std::unique_ptr<AttributeList> attributes)
      : SourceElement(element),
        type_ctor(std::move(type_ctor)),
        identifier(std::move(identifier)),
        attributes(std::move(attributes)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<TypeConstructor> type_ctor;
  std::unique_ptr<Identifier> identifier;
  std::unique_ptr<AttributeList> attributes;
};

class ServiceDeclaration final : public SourceElement {
 public:
  ServiceDeclaration(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
                     std::unique_ptr<Identifier> identifier,
                     std::vector<std::unique_ptr<ServiceMember>> members)
      : SourceElement(element),
        attributes(std::move(attributes)),
        identifier(std::move(identifier)),
        members(std::move(members)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Identifier> identifier;
  std::vector<std::unique_ptr<ServiceMember>> members;
};
class LayoutMember : public SourceElement {
 public:
  enum Kind {
    kOrdinaled,
    kStruct,
    kValue,
  };

  explicit LayoutMember(SourceElement const& element, Kind kind,
                        std::unique_ptr<AttributeList> attributes,
                        std::unique_ptr<Identifier> identifier)
      : SourceElement(element),
        kind(kind),
        attributes(std::move(attributes)),
        identifier(std::move(identifier)) {}

  void Accept(TreeVisitor* visitor) const;

  const Kind kind;
  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Identifier> identifier;
};

class OrdinaledLayoutMember final : public LayoutMember {
 public:
  explicit OrdinaledLayoutMember(SourceElement const& element,
                                 std::unique_ptr<AttributeList> attributes,
                                 std::unique_ptr<Ordinal64> ordinal,
                                 std::unique_ptr<Identifier> identifier,
                                 std::unique_ptr<TypeConstructor> type_ctor)
      : LayoutMember(element, Kind::kOrdinaled, std::move(attributes), std::move(identifier)),
        ordinal(std::move(ordinal)),
        type_ctor(std::move(type_ctor)) {}
  explicit OrdinaledLayoutMember(SourceElement const& element,
                                 std::unique_ptr<AttributeList> attributes,
                                 std::unique_ptr<Ordinal64> ordinal)
      : LayoutMember(element, Kind::kOrdinaled, std::move(attributes), nullptr),
        ordinal(std::move(ordinal)),
        type_ctor(nullptr),
        reserved(true) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<Ordinal64> ordinal;
  std::unique_ptr<TypeConstructor> type_ctor;
  const bool reserved = false;
};

class ValueLayoutMember final : public LayoutMember {
 public:
  explicit ValueLayoutMember(SourceElement const& element,
                             std::unique_ptr<AttributeList> attributes,
                             std::unique_ptr<Identifier> identifier,
                             std::unique_ptr<Constant> value)
      : LayoutMember(element, Kind::kValue, std::move(attributes), std::move(identifier)),
        value(std::move(value)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<Constant> value;
};

class StructLayoutMember final : public LayoutMember {
 public:
  explicit StructLayoutMember(SourceElement const& element,
                              std::unique_ptr<AttributeList> attributes,
                              std::unique_ptr<Identifier> identifier,
                              std::unique_ptr<TypeConstructor> type_ctor,
                              std::unique_ptr<Constant> default_value)
      : LayoutMember(element, Kind::kStruct, std::move(attributes), std::move(identifier)),
        type_ctor(std::move(type_ctor)),
        default_value(std::move(default_value)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<TypeConstructor> type_ctor;
  std::unique_ptr<Constant> default_value;
};

class Layout final : public SourceElement {
 public:
  enum Kind {
    kBits,
    kEnum,
    kStruct,
    kTable,
    kUnion,
  };

  Layout(SourceElement const& element, Kind kind,
         std::vector<std::unique_ptr<LayoutMember>> members, std::unique_ptr<Modifiers> modifiers,
         std::unique_ptr<TypeConstructor> subtype_ctor)
      : SourceElement(element),
        kind(kind),
        members(std::move(members)),
        modifiers(std::move(modifiers)),
        subtype_ctor(std::move(subtype_ctor)) {}

  void Accept(TreeVisitor* visitor) const;

  Kind kind;
  std::vector<std::unique_ptr<raw::LayoutMember>> members;
  // TODO(fxbug.dev/79094): refactor this to only have a single null state.
  std::unique_ptr<Modifiers> modifiers;
  // TODO(fxbug.dev/77853): Eventually we'll make [Struct/Ordinaled/Value]Layout
  //  classes to inherit from the now-abstract Layout class, similar to what can
  //  currently be seen on LayoutMember and its children.  When that happens
  //  this field will only exist on ValueLayout.
  std::unique_ptr<TypeConstructor> subtype_ctor;
};

class LayoutReference : public SourceElement {
 public:
  enum Kind {
    kInline,
    kNamed,
  };

  LayoutReference(SourceElement const& element, Kind kind) : SourceElement(element), kind(kind) {}

  void Accept(TreeVisitor* visitor) const;

  const Kind kind;
};

class InlineLayoutReference final : public LayoutReference {
 public:
  explicit InlineLayoutReference(SourceElement const& element,
                                 std::unique_ptr<AttributeList> attributes,
                                 std::unique_ptr<Layout> layout)
      : LayoutReference(element, Kind::kInline),
        attributes(std::move(attributes)),
        layout(std::move(layout)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Layout> layout;
};

class NamedLayoutReference final : public LayoutReference {
 public:
  explicit NamedLayoutReference(SourceElement const& element,
                                std::unique_ptr<CompoundIdentifier> identifier)
      : LayoutReference(element, Kind::kNamed), identifier(std::move(identifier)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<CompoundIdentifier> identifier;
};

class LayoutParameter : public SourceElement {
 public:
  enum Kind {
    kIdentifier,
    kLiteral,
    kType,
  };

  LayoutParameter(SourceElement const& element, Kind kind) : SourceElement(element), kind(kind) {}

  void Accept(TreeVisitor* visitor) const;

  const Kind kind;
};

class LiteralLayoutParameter final : public LayoutParameter {
 public:
  explicit LiteralLayoutParameter(SourceElement const& element,
                                  std::unique_ptr<LiteralConstant> literal)
      : LayoutParameter(element, Kind::kLiteral), literal(std::move(literal)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<LiteralConstant> literal;
};

class TypeLayoutParameter final : public LayoutParameter {
 public:
  explicit TypeLayoutParameter(SourceElement const& element,
                               std::unique_ptr<TypeConstructor> type_ctor)
      : LayoutParameter(element, Kind::kType), type_ctor(std::move(type_ctor)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<TypeConstructor> type_ctor;
};

class IdentifierLayoutParameter final : public LayoutParameter {
 public:
  explicit IdentifierLayoutParameter(SourceElement const& element,
                                     std::unique_ptr<CompoundIdentifier> identifier)
      : LayoutParameter(element, Kind::kIdentifier), identifier(std::move(identifier)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<CompoundIdentifier> identifier;
};

class LayoutParameterList final : public SourceElement {
 public:
  LayoutParameterList(SourceElement const& element,
                      std::vector<std::unique_ptr<raw::LayoutParameter>> items)
      : SourceElement(element), items(std::move(items)) {}

  void Accept(TreeVisitor* visitor) const;

  std::vector<std::unique_ptr<raw::LayoutParameter>> items;
};

class TypeConstraints final : public SourceElement {
 public:
  TypeConstraints(SourceElement const& element, std::vector<std::unique_ptr<raw::Constant>> items)
      : SourceElement(element), items(std::move(items)) {}

  void Accept(TreeVisitor* visitor) const;

  std::vector<std::unique_ptr<raw::Constant>> items;
};

class TypeDecl final : public SourceElement {
 public:
  TypeDecl(SourceElement const& element, std::unique_ptr<AttributeList> attributes,
           std::unique_ptr<Identifier> identifier, std::unique_ptr<TypeConstructor> type_ctor)
      : SourceElement(element),
        attributes(std::move(attributes)),
        identifier(std::move(identifier)),
        type_ctor(std::move(type_ctor)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<AttributeList> attributes;
  std::unique_ptr<Identifier> identifier;
  std::unique_ptr<TypeConstructor> type_ctor;
};

class File final : public SourceElement {
 public:
  File(SourceElement const& element, Token end, std::unique_ptr<LibraryDecl> library_decl,
       std::vector<std::unique_ptr<AliasDeclaration>> alias_list,
       std::vector<std::unique_ptr<Using>> using_list,
       std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list,
       std::vector<std::unique_ptr<ProtocolDeclaration>> protocol_declaration_list,
       std::vector<std::unique_ptr<ResourceDeclaration>> resource_declaration_list,
       std::vector<std::unique_ptr<ServiceDeclaration>> service_declaration_list,
       std::vector<std::unique_ptr<TypeDecl>> type_decls,
       std::vector<std::unique_ptr<Token>> tokens)
      : SourceElement(element),
        library_decl(std::move(library_decl)),
        alias_list(std::move(alias_list)),
        using_list(std::move(using_list)),
        const_declaration_list(std::move(const_declaration_list)),
        protocol_declaration_list(std::move(protocol_declaration_list)),
        resource_declaration_list(std::move(resource_declaration_list)),
        service_declaration_list(std::move(service_declaration_list)),
        type_decls(std::move(type_decls)),
        tokens(std::move(tokens)),
        end_(end) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<LibraryDecl> library_decl;
  std::vector<std::unique_ptr<AliasDeclaration>> alias_list;
  std::vector<std::unique_ptr<Using>> using_list;
  std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
  std::vector<std::unique_ptr<ProtocolDeclaration>> protocol_declaration_list;
  std::vector<std::unique_ptr<ResourceDeclaration>> resource_declaration_list;
  std::vector<std::unique_ptr<ServiceDeclaration>> service_declaration_list;
  std::vector<std::unique_ptr<TypeDecl>> type_decls;

  // An ordered list of all tokens (including comments) in the source file.
  std::vector<std::unique_ptr<Token>> tokens;
  Token end_;
};

}  // namespace fidl::raw

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_RAW_AST_H_
